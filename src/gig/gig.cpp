// recursive ogm bindings
// allows interpreting byteecode within an interpretation context

#ifdef _WIN32
#define external          \
    extern "C"            \
    __declspec(dllexport)
#else
#define external          \
    extern "C"
#endif

#define TEST_INIT size_t testnum = 0;
#define TEST_ASSERT(x) {if (!(x)) {printf("Assertion failed on line %d!\n", __LINE__); return -1;} else {testnum++;}}
#define TEST_END {printf("All %d tests passed.\n\n", testnum); return 0;}

typedef double ty_real;
typedef const char* ty_string;

#include <ogm/bytecode/bytecode.hpp>
#include <ogm/bytecode/BytecodeTable.hpp>
#include <ogm/bytecode/stream_macro.hpp>
#include <ogm/common/error.hpp>
#include <ogm/ast/parse.h>

#if __has_include("src/common/license.inc")
#include "src/common/license.inc"
#define OGM_LICENSE_AVAILABLE
#endif

#include <iostream>
#include <vector>
#include <exception>
#include <sstream>

using namespace ogm::bytecode;
using namespace ogm::bytecode::opcode;

struct ParseResult
{
    bool m_error = false;
    std::string m_what_error;
    ReflectionAccumulator m_reflection_accumulator;
    std::vector<DisassembledBytecodeInstruction> m_instructions;
	// Explicitly defined destructor to ensure `ReflectionAccumulator`'s destructor is called
    ~ParseResult() = default;
};

std::vector<ParseResult*> results;

struct GigLibrary : public Library
{
    bool generate_function_bytecode(std::ostream& out, const char* functionName, unsigned char argc) const
    {
        // all function names are permitted; the caller will discriminate between valid
        // and invalid function names.
        // write the function name directly as the immediate for the nat opcode.

        write_op(out, nat);
        out << functionName;

        // write argument count
        out << ':' << (int)argc;

        out << (char)0;

        return true;
    }

    // ignored. The caller will read the reflection accumulator to discern
    // variable names from constant values.
    bool generate_constant_bytecode(std::ostream& out, const char* constantName) const
    { return false; }

    // ignored. The caller wil discriminate between variables and built-in variables.
    bool variable_definition(const char* variableName, BuiltInVariableDefinition&) const
    { return false; }

    virtual bool dis_function_name(BytecodeStream& in, std::ostream& out) const
    {
        // just read everything.
        std::string name_and_args;
        char c = 1;
        while (c)
        {
            in >> c;
            if (c != 0)
            {
                name_and_args += c;
            }
        }
        out << name_and_args;
        return true;
    }
} k_gigLibrary;

// compiles the given string into bytecode
// returns an index which the compiled result can then be accessed with.
external ty_real gig_generate(ty_string code)
{
    results.emplace_back(new ParseResult());
    ParseResult& pr = *results.back();
    try
    {
        ogm_ast_t* ast = ogm_ast_parse(code);
        if (ast)
        {
            // dummy input (TODO: ProjectAccumulator shouldn't need these.)
            ogm::asset::AssetTable at;
            ogm::bytecode::BytecodeTable bt;
            ogm::asset::Config c;

            ProjectAccumulator pacc{ &k_gigLibrary, &pr.m_reflection_accumulator, &at, &bt, &c };
            k_gigLibrary.reflection_add_instance_variables(*pacc.m_reflection);
            ogm::bytecode_index_t index = bytecode_generate(
                DecoratedAST(
                    ast, "gig-generated code", "", 1, 0
                ),
                pacc
            );
            ogm::bytecode::Bytecode bytecode = bt.get_bytecode(index);
            bytecode_dis(bytecode, pr.m_instructions, &k_gigLibrary, true);
            pr.m_error = false;
        }
        else
        {
            pr.m_error = true;
        }
        ogm_ast_free(ast);
    }
    catch (const std::exception& e)
    {
        pr.m_error = true;
        pr.m_what_error = e.what();
    }
    return results.size() - 1;
}

external ty_real gig_error(ty_real rindex)
{
    size_t index = rindex;
    return results[index]->m_error;
}

external ty_string gig_what_error(ty_real rindex)
{
    size_t index = rindex;
    return results[index]->m_what_error.c_str();
}

external ty_real gig_variable_instance_count(ty_real rindex)
{
    size_t index = rindex;
    return results[index]->m_reflection_accumulator.m_namespace_instance.id_count();
}

external ty_string gig_variable_instance_name(ty_real rindex, ty_real rvindex)
{
    size_t index = rindex;
    size_t vindex = rvindex;
    return results[index]->m_reflection_accumulator.m_namespace_instance.find_name(vindex);
}

external ty_real gig_variable_global_count(ty_real rindex)
{
    size_t index = rindex;
    return results[index]->m_reflection_accumulator.m_namespace_instance.id_count();
}

external ty_string gig_variable_global_name(ty_real rindex, ty_real rvindex)
{
    size_t index = rindex;
    size_t vindex = rvindex;
    return results[index]->m_reflection_accumulator.m_namespace_instance.find_name(vindex);
}

external ty_real gig_instruction_count(ty_real rindex)
{
    size_t index = rindex;
    return results[index]->m_instructions.size();
}

external ty_string gig_instruction_opcode(ty_real rindex, ty_real rinstruction)
{
    size_t index = rindex;
    size_t instruction = rinstruction;
    return ty_string(get_opcode_string(results[index]->m_instructions[instruction].m_op));//return get_opcode_string(results[index]->m_instructions[instruction].m_op);
}

external ty_string gig_instruction_immediate(ty_real rindex, ty_real rinstruction)
{
    size_t index = rindex;
    size_t instruction = rinstruction;
    return results[index]->m_instructions[instruction].m_immediate.c_str();
}

external ty_real gig_instruction_address(ty_real rindex, ty_real rinstruction)
{
    size_t index = rindex;
    size_t instruction = rinstruction;
    return results[index]->m_instructions[instruction].m_address;
}

external ty_real gig_2d_arrays()
{
    #ifdef OGM_2DARRAY
    return true;
    #else
    return false;
    #endif
}

external ty_real gig_free(ty_real rindex)
{
    size_t index = rindex;
    delete(results[index]);
	results.erase(results.begin() + index);
	/*if (results.begin() + index != results.end() - 1)
	{
		return -77;
		//pr.m_error = true;
        //pr.m_what_error = "STACK CORRUPTION";
	}*/
    return 0;
}

external ty_string gig_hello()
{
    static char hello[32] = "hello";
    return hello;
}

external ty_real gig_version()
{
    return 101;
}

external ty_string gig_license()
{
    #ifdef OGM_LICENSE_AVAILABLE
    return _ogm_license_;
    #else
    return "";
    #endif
}

// replacement for missing ogm-sys functions
// (for maximum portability, we don't includ ogm-sys in gig)
namespace ogm
{
    bool terminal_supports_colours()
    {
        return false;
    }
}