#include <cstdint>
#include <utility>
#include <vector>

#include "fuzz_harness.hpp"
#include "runtime/compiler/bytecode_serializer.hpp"

// LibFuzzer entry point for the .lumc bytecode deserializer.
//
// BytecodeSerializer::deserialize is a hand-written, big-endian binary reader
// fed by an untrusted trust boundary: cache files (.lumc) loaded at start-up
// and, by design, pre-compiled module distribution.  Arbitrary bytes must
// never crash it, read out of bounds, or exhaust memory unboundedly.
//
// When a buffer does deserialize, the target also checks a round-trip oracle:
// re-serialising the decoded program and decoding it again must reproduce the
// exact same bytes.  Serialisation is deterministic and symmetric with
// deserialisation, so any divergence indicates an asymmetry bug in the format.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > luma::fuzz::max_input_size) {
        return 0;
    }

    const std::vector<std::uint8_t> bytes(data, data + size);

    return luma::fuzz::run([&] {
        auto first = luma::BytecodeSerializer::deserialize(bytes);
        luma::fuzz::do_not_optimize(first.error);
        luma::fuzz::do_not_optimize(first.detail.size());
        if (!first) {
            return; // Malformed input — rejection is the expected outcome.
        }

        // Round-trip oracle: a program that came out of the deserialiser must
        // serialise to a buffer that deserialises again to identical bytes.
        const auto encoded = luma::BytecodeSerializer::serialize(
            first->top_level, first->functions, first->header.source_hash, first->header.timestamp);

        auto second = luma::BytecodeSerializer::deserialize(encoded);
        if (!second) {
            luma::fuzz::trap(); // Freshly serialised bytecode failed to decode.
        }

        const auto re_encoded = luma::BytecodeSerializer::serialize(
            second->top_level, second->functions, second->header.source_hash,
            second->header.timestamp);

        if (encoded != re_encoded) {
            luma::fuzz::trap(); // Serialisation is not deterministic/stable.
        }
    });
}
