/**
 * @file op_0b_init_mock_node_attr.inc
 * @brief Formal Contract for OP_INIT_MOCK_NODE_ATTR (0x0B).
 *
 * Mnemonic: OP_INIT_MOCK_NODE_ATTR
 * Opcode: 0x0B
 * Description: Binds mock node attribute buffer from vector register with optional validity mask.
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
#ifndef OP_0B_CONTRACT_DEFINED
#define OP_0B_CONTRACT_DEFINED 1
#endif

// Opcode 0x0B: OP_INIT_MOCK_NODE_ATTR
op_INIT_MOCK_NODE_ATTR: {
    impulse_vm_status_t st = execute_init_mock_attr<true>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
