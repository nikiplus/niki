#include "niki/l0_core/ir/module_ir.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace niki::ir {
//---toString helpers---

const char *toString(IRValueKind val_kind) {
    switch (val_kind) {
    case IRValueKind::Invalid:
        return "Invalid";
    case IRValueKind::VReg:
        return "VReg";
    case IRValueKind::ImmI64:
        return "ImmI64";
    case IRValueKind::ImmF64:
        return "ImmF64";
    case IRValueKind::ImmBool:
        return "ImmBool";
    case IRValueKind::StringId:
        return "StringId";
    case IRValueKind::SymbolId:
        return "SymbolId";
    case IRValueKind::BlockId:
        return "BlockId";
    case IRValueKind::FuncId:
        return "FuncId";
    }
    return "UnknownIRValueKind";
}

const char *toString(IRInstKind inst_kind) {
    switch (inst_kind) {
    case IRInstKind::Nop:
        return "Nop";
    case IRInstKind::Constant:
        return "Constant";
    case IRInstKind::Move:
        return "Move";
    case IRInstKind::Add:
        return "Add";
    case IRInstKind::Sub:
        return "Sub";
    case IRInstKind::Mul:
        return "Mul";
    case IRInstKind::Div:
        return "Div";
    case IRInstKind::Mod:
        return "Mod";
    case IRInstKind::Neg:
        return "Neg";
    case IRInstKind::CmpEq:
        return "CmpEq";
    case IRInstKind::CmpNe:
        return "CmpNe";
    case IRInstKind::CmpLt:
        return "CmpLt";
    case IRInstKind::CmpLe:
        return "CmpLe";
    case IRInstKind::CmpGt:
        return "CmpGt";
    case IRInstKind::CmpGe:
        return "CmpGe";
    case IRInstKind::LogicAnd:
        return "LogicAnd";
    case IRInstKind::LogicOr:
        return "LogicOr";
    case IRInstKind::LogicNot:
        return "LogicNot";
    case IRInstKind::LoadGlobal:
        return "LoadGlobal";
    case IRInstKind::StoreGlobal:
        return "StoreGlobal";
    case IRInstKind::Call:
        return "Call";
    case IRInstKind::Return:
        return "Return";
    case IRInstKind::NewArray:
        return "NewArray";
    case IRInstKind::PushArray:
        return "PushArray";
    case IRInstKind::NewMap:
        return "NewMap";
    case IRInstKind::SetMap:
        return "SetMap";
    case IRInstKind::GetIndex:
        return "GetIndex";
    case IRInstKind::SetIndex:
        return "SetIndex";
    case IRInstKind::GetMember:
        return "GetMember";
    case IRInstKind::SetMember:
        return "SetMember";
    case IRInstKind::Jump:
        return "Jump";
    case IRInstKind::Branch:
        return "Branch";
    case IRInstKind::Phi:
        return "Phi";
    }
    return "UnknownIRInstKind";
}

const char *toString(IRSymbolKind sym_kind) {
    switch (sym_kind) {
    case IRSymbolKind::Function:
        return "Function";
    case IRSymbolKind::Struct:
        return "Struct";
    case IRSymbolKind::GlobalVar:
        return "GlobalVar";
    case IRSymbolKind::External:
        return "External";
    }
    return "UnknownIRSymbolKind";
}

//---find helpers---
const IRFunction *findFuncById(const ModuleIR &mod_ir, IRFunctionId func_id) {
    if (func_id >= mod_ir.func_table.size()) {
        return nullptr;
    }
    return &mod_ir.func_table[func_id];
}

IRFunction *findFuncById(ModuleIR &mod_ir, IRFunctionId func_id) {
    if (func_id >= mod_ir.func_table.size()) {
        return nullptr;
    }
    return &mod_ir.func_table[func_id];
}

const IRBasicBlock *findBlockById(const IRFunction &func_ir, IRBlockId block_id) {
    if (block_id >= func_ir.basic_blocks.size()) {
        return nullptr;
    }
    return &func_ir.basic_blocks[block_id];
}

IRBasicBlock *findBlockById(IRFunction &func_ir, IRBlockId block_id) {
    if (block_id >= func_ir.basic_blocks.size()) {
        return nullptr;
    }
    return &func_ir.basic_blocks[block_id];
}

const IRSymbol *findSymbolById(const ModuleIR &module_ir, IRSymbolId symbol_identifier) {
    if (symbol_identifier >= module_ir.sym_table.size()) {
        return nullptr;
    }
    return &module_ir.sym_table[symbol_identifier];
}
IRSymbol *findSymbolById(ModuleIR &module_ir, IRSymbolId symbol_identifier) {
    if (symbol_identifier >= module_ir.sym_table.size()) {
        return nullptr;
    }
    return &module_ir.sym_table[symbol_identifier];
}
//---format helpers---
std::string formatValue(const IRValue &value) {
    std::ostringstream output;
    switch (value.value_kind) {
    case IRValueKind::Invalid:
        output << "invalid";
        break;
    case IRValueKind::VReg:
        output << "v" << value.payload_as_u32;
        break;
    case IRValueKind::ImmI64:
        output << value.payload_as_i64;
        break;
    case IRValueKind::ImmF64:
        output << "f64_bits(0x" << std::hex << value.payload_as_u64 << std::dec << ")";
        break;
    case IRValueKind::ImmBool:
        output << (value.payload_as_u32 != 0 ? "true" : "false");
        break;
    case IRValueKind::StringId:
        output << "str#" << value.payload_as_u32;
        break;
    case IRValueKind::SymbolId:
        output << "sym#" << value.payload_as_u32;
        break;
    case IRValueKind::BlockId:
        output << "bb#" << value.payload_as_u32;
        break;
    case IRValueKind::FuncId:
        output << "fn#" << value.payload_as_u32;
        break;
    }
    return output.str();
}

std::string dumpInstruction(const IRInst &inst, uint32_t inst_index) {
    std::ostringstream output;

    output << " [" << inst_index << "] ";
    output << toString(inst.instruction_kind);

    output << " dst = " << formatValue(inst.destination_value);
    output << " a=" << formatValue(inst.first_operand);
    output << " b=" << formatValue(inst.second_operand);
    output << " c=" << formatValue(inst.third_operand);

    if (inst.auxiliary_data != 0) {
        output << " aux= " << inst.auxiliary_data;
    }

    if (inst.source_line != 0 || inst.source_column != 0) {
        output << " @(" << inst.source_line << "," << inst.source_column << ")";
    }

    return output.str();
}

std::string dumpFunction(const IRFunction &func_ir) {
    std::ostringstream output;

    output << "  function #" << func_ir.func_id;
    output << " name_id=" << func_ir.func_name_id;
    output << " entry_block=" << func_ir.entry_block_id;
    output << " next_vreg=" << func_ir.next_vreg_id;
    output << " source=\"" << func_ir.func_src_path << "\"\n";

    for (size_t index = 0; index < func_ir.func_sig.parameter_types.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << static_cast<uint32_t>(func_ir.func_sig.parameter_types[index].handle);
    }
    output << ") -> " << static_cast<uint32_t>(func_ir.func_sig.return_type.handle) << "\n";

    output << "   params: [";
    for (size_t index = 0; index < func_ir.parameter_registers.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "v" << func_ir.parameter_registers[index];
    }
    output << "]\n";

    for (const IRBasicBlock &basic_block : func_ir.basic_blocks) {
        output << "   block #" << basic_block.block_id;
        if (!basic_block.debug_block_name.empty()) {
            output << "\"" << basic_block.debug_block_name << "\"";
        }
        output << "\n";

        for (size_t inst_index = 0; inst_index < basic_block.instruction_list.size(); ++inst_index) {
            output << dumpInstruction(basic_block.instruction_list[inst_index], static_cast<uint32_t>(inst_index))
                   << "\n";
        }
    }
    return output.str();
}

std::string dumpModule(const ModuleIR &module_ir) {
    std::ostringstream output;

    output << "ModuleIR \"" << module_ir.module_name << "\"\n";
    output << "   source:" << module_ir.module_src_path << "\n";
    output << "   initializer: ";
    if (module_ir.module_initializer_func_id == std::numeric_limits<IRFunctionId>::max()) {
        output << "none\n";
    } else {
        output << module_ir.module_initializer_func_id << "\n";
    }

    output << "  string_pool_size: " << module_ir.module_string_pool.size() << "\n";
    output << "  function_count: " << module_ir.func_table.size() << "\n";
    output << "  symbol_count: " << module_ir.sym_table.size() << "\n";
    output << "  symbols:\n";
    for (const IRSymbol &symbol : module_ir.sym_table) {
        output << "    sym#" << symbol.sym_id << " ";
        output << toString(symbol.sym_kind);
        output << " name_id=" << symbol.sym_name_id;
        output << " exported=" << (symbol.is_exported ? "true" : "false");
        output << " owner=\"" << symbol.owner_mod_path << "\"";
        if (symbol.owner_func_id != std::numeric_limits<IRFunctionId>::max()) {
            output << " owner_fn=" << symbol.owner_func_id;
        }
        output << "\n";
    }
    output << "  functions:\n";
    for (const IRFunction &function_ir : module_ir.func_table) {
        output << dumpFunction(function_ir);
    }
    return output.str();
}



} // namespace niki::ir