{
  {
     let a := 0x20
     mstore(a, 2)
  }
  let a
  mstore(a, 3)
}
// ----
// Trace:
//   MSTORE_AT_SIZE(32, 32) [0000000000000000000000000000000000000000000000000000000000000002]
//   MSTORE_AT_SIZE(0, 32) [0000000000000000000000000000000000000000000000000000000000000003]
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000000003
//     20: 0000000000000000000000000000000000000000000000000000000000000002
// Storage dump:
