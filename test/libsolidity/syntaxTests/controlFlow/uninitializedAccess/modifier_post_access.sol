contract C {
    uint[] s;
    modifier mod(uint[] storage b) {
        _;
        b[0] = 0;
    }
    function f() mod(a) internal returns (uint[] storage a)
    {
		a = s;
    }
}
// ----
// TypeError: (120-121): This variable is of storage pointer type and is accessed without prior assignment.