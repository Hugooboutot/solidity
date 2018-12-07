contract C {
    function f() internal pure returns (mapping(uint=>uint) storage r) { }
}
// ----
// TypeError: (53-82): This variable is of storage pointer type and is accessed without prior assignment.
