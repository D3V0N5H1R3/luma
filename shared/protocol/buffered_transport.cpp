#include "protocol/buffered_transport.hpp"

#include <algorithm>
#include <format>

#include "protocol/transport_exceptions.hpp"

namespace luma::protocol {

bool BufferedTransport::refill() {
    const auto count = read_raw(std::span<char>{read_buf_});

    if (count == 0) {
        return false;
    }

    read_pos_ = 0;
    read_end_ = count;
    return true;
}

bool BufferedTransport::ensure_buffered() {
    if (read_pos_ < read_end_) {
        return true;
    }

    return refill();
}

void BufferedTransport::enforce_header_length(const std::string& line) const {
    if (line.size() > max_header_length()) {
        throw ParseError(std::format("Header line exceeds {} byte limit", max_header_length()));
    }
}

std::optional<std::string> BufferedTransport::read_line() {
    std::string line;

    while (true) {
        if (!ensure_buffered()) {
            if (line.empty()) {
                return std::nullopt;
            }

            return line;
        }

        // Scan the buffer for '\n' and append the whole chunk at once.
        const char* start = read_buf_ + read_pos_;
        const char* end = read_buf_ + read_end_;
        const char* newline = std::find(start, end, '\n');

        if (newline != end) {
            // Found '\n' — append everything up to and including it.
            const auto chunk_len = static_cast<std::size_t>(newline - start + 1);
            line.append(start, chunk_len);
            read_pos_ += chunk_len;

            enforce_header_length(line);

            // Strip trailing \r\n.
            if (line.size() >= 2 && line[line.size() - 2] == '\r' &&
                line[line.size() - 1] == '\n') {
                line.resize(line.size() - 2);
            } else if (line.back() == '\n') {
                line.pop_back();
            }

            return line;
        }

        // No newline found — append the entire remaining buffer.
        const auto chunk_len = static_cast<std::size_t>(end - start);
        line.append(start, chunk_len);
        read_pos_ = read_end_;

        enforce_header_length(line);
    }
}

std::string BufferedTransport::read_exact(std::size_t count) {
    std::string result;
    result.reserve(count);

    while (result.size() < count) {
        if (!ensure_buffered()) {
            throw ConnectionClosed(std::format("EOF after {} of {} bytes", result.size(), count));
        }

        // Copy as much as possible from the buffer.
        const auto available = read_end_ - read_pos_;
        const auto remaining = count - result.size();
        const auto chunk = std::min(available, remaining);

        result.append(read_buf_ + read_pos_, chunk);
        read_pos_ += chunk;
    }

    return result;
}

} // namespace luma::protocol
