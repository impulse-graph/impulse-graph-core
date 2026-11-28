// Opcode 0x21: OP_INIT_DOMAIN_SPAN
VM_OP_CASE(INIT_DOMAIN_SPAN) {
    const uint8_t dst = IMP_DST(w);
    const uint16_t dom_id = IMP_IMM16_LO(w);
    uint64_t dom_card = 0;
    impulse_vm_status_t st = vm_lookup_domain_card(snap, dom_id, &dom_card);
    if (st != IMPULSE_VM_OK) {
        ws.status = st;
    } else {
        ws.f_regs[dst].init_all_span(dom_id, dom_card);
        ws.set_f_init(dst, true);
        ws.set_zf(dom_card == 0ULL);
        ws.state.pc += 1u;
    }
    VM_DISPATCH();
}
