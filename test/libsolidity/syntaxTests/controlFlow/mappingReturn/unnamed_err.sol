contract C {
    function f() internal pure returns (mapping(uint=>uint) storage) {}
}
// ----
// TypeError: (53-80): This variable is of storage pointer type and is accessed without prior assignment.
