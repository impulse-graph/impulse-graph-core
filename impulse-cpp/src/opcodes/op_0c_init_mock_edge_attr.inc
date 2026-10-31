/**
 * @file op_0c_init_mock_edge_attr.inc
 * @brief Formal Contract for OP_INIT_MOCK_EDGE_ATTR (0x0C).
 *
 * Mnemonic: OP_INIT_MOCK_EDGE_ATTR
 * Opcode: 0x0C
 * Description: Binds mock edge attribute buffer from vector register with optional validity mask.
 *
 * Operands:
 *  - attr_id = inst.dst_reg, src = payload & 0xFF, mask = (payload >> 8) & 0xFF
 *
 * Inputs:
 *  - src: TYPE_FLOAT_VECTOR | TYPE_NODE_VECTOR, mask: TYPE_BITSET_HANDLE (optional)
 *
 * Outputs:
 *  - Internal query_context mock attribute slot
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context mock attribute slots)
 */
#ifndef OP_0C_CONTRACT_DEFINED
#define OP_0C_CONTRACT_DEFINED 1
#endif

// Opcode 0x0C: OP_INIT_MOCK_EDGE_ATTR
op_INIT_MOCK_EDGE_ATTR: {
    impulse_vm_status_t st = execute_init_mock_attr<false>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
