#include "impulse_compiler.hpp"
#include <iostream>

using namespace impulse::compiler;

int main() {
    auto w = ScmWalk::forward("test");
    auto p = std::make_shared<ScmProgram>(std::vector<AstPtr>{w});
    
    CompilerOptions c_opt;
    // Wait, is there c_opt.enable_direction_selection? Let's just pass it.
    auto bytecode = ImpulseCompiler::compile(p, nullptr, {}, c_opt);
    
    if (bytecode.instructions[0].opcode == OP_ADAPTIVE_WALK) {
        std::cout << "SUCCESS: Emitted OP_ADAPTIVE_WALK!" << std::endl;
    } else {
        std::cout << "FAIL: Emitted opcode " << (int)bytecode.instructions[0].opcode << std::endl;
    }
    return 0;
}
