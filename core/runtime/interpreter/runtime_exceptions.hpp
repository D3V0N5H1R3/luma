// ─────────────────────────────────────────────────────────────────────────────
// runtime_exceptions.hpp — Typed exception hierarchy for VM runtime errors.
// ─────────────────────────────────────────────────────────────────────────────
// Provides fine-grained exception types that inherit from RuntimeError so
// that the VM dispatch loop's existing catch(const RuntimeError&) handler
// intercepts them automatically.
//
// These exceptions implement the "exceptions for bugs" convention — see
// runtime/interpreter/value_type.hpp for the full error signaling guide.
//
// Hierarchy:
//
//   RuntimeError (analysis/errors/error.hpp)
//   ├── VMError               — base for internal VM faults
//   │   ├── StackError        — stack overflow / underflow / restore fault
//   │   └── BytecodeError     — invalid opcode or dispatch fault
//   └── ChannelError          — base for channel operation faults
//       ├── ChannelClosedError    — send/receive on a closed channel
//       ├── ChannelFullError      — non-blocking send on a full channel
//       └── ChannelEmptyError     — non-blocking receive on an empty channel
//
// ── Catch-site protocol ──────────────────────────────────────────────────────
// All existing catch sites in the codebase use catch(const RuntimeError&),
// which intercepts every subclass in this hierarchy automatically.  No catch
// site currently differentiates between subtypes.  If a caller ever needs to
// handle one distinctly (e.g. to surface a friendlier diagnostic or to reset
// state before retrying), it should catch that type before the generic
// RuntimeError handler:
//
//   } catch (const ChannelClosedError& e) {
//       // Handle channel-specific recovery.
//   } catch (const RuntimeError& e) {
//       // Fallback for all other runtime errors.
//   }
//
// ── Stack faults ─────────────────────────────────────────────────────────────
// Stack faults originate in VMStack where no SourceLocation is available, so
// VMError and StackError accept a defaulted empty SourceLocation.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_RUNTIME_RUNTIME_EXCEPTIONS_HPP
#define LUMA_RUNTIME_RUNTIME_EXCEPTIONS_HPP

#include "analysis/errors/error.hpp"

namespace luma {

// Base for internal VM exceptions (stack faults, bytecode errors).
class VMError : public RuntimeError {
public:
    using RuntimeError::RuntimeError;
};

// Stack overflow, underflow, or restore fault.
class StackError : public VMError {
public:
    using VMError::VMError;
};

// Invalid opcode or dispatch fault.
class BytecodeError : public VMError {
public:
    using VMError::VMError;
};

// ── Channel errors ──────────────────────────────────────────────────────────

// Base for all channel operation faults.  A shared base lets call sites that
// treat every channel failure identically (e.g. Channel.receive_all and
// Channel.select, which just move on) use a single catch without widening to
// the generic RuntimeError.  Existing catch(const RuntimeError&) sites still
// intercept these because ChannelError derives from RuntimeError.
class ChannelError : public RuntimeError {
public:
    using RuntimeError::RuntimeError;
};

// Sending to or receiving from a channel that has been closed.
class ChannelClosedError : public ChannelError {
public:
    using ChannelError::ChannelError;
};

// Non-blocking send encountered a full channel buffer.
class ChannelFullError : public ChannelError {
public:
    using ChannelError::ChannelError;
};

// Non-blocking receive found an empty channel buffer.
class ChannelEmptyError : public ChannelError {
public:
    using ChannelError::ChannelError;
};

} // namespace luma

#endif // LUMA_RUNTIME_RUNTIME_EXCEPTIONS_HPP
