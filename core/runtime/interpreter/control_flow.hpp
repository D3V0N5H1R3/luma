#ifndef LUMA_INTERPRETER_CONTROL_FLOW_HPP
#define LUMA_INTERPRETER_CONTROL_FLOW_HPP

namespace luma {

// ExitSignal — thrown by Process.exit() to unwind the VM and terminate with an
// exit code.  Caught at the top level in main().
struct ExitSignal {
    int code{0};
};

} // namespace luma

#endif // LUMA_INTERPRETER_CONTROL_FLOW_HPP
