#include "generate.hpp"
#include "lvalue.hpp"

#include "ogm/bytecode/bytecode.hpp"
#include "ogm/ast/parse.h"

#include "ogm/common/error.hpp"
#include "ogm/common/util.hpp"

#include <cstring>
#include <map>
#include <string>
#include <cassert>

// && and || skip second term if result is known.
#define SHORTCIRCUIT_EVALUATIONS

namespace ogm::bytecode
{

const EmptyLibrary defaultLibrary;

using namespace ogm::asset;
using namespace opcode;

const BytecodeTable defaultBytecodeTable;

struct EnumData {
    std::map<std::string, ogm_ast_t*> m_map;

    ~EnumData()
    {
        for (const auto& pair : m_map)
        {
            delete std::get<1>(pair);
        }
    }
};

// FIXME: put this in its own header file.
// Having it just in this cpp file is very awkward,
// because it necessitates ReflectionAccumulator::ReflectionAccumulator()
struct EnumTable {
    std::map<std::string, EnumData> m_map;
};

ReflectionAccumulator::ReflectionAccumulator()
    : m_namespace_instance()
    , m_bare_globals()
    , m_ast_macros()
    , m_enums(new EnumTable())
{ }

ReflectionAccumulator::~ReflectionAccumulator()
{
    // free macro ASTs
    for (auto& pair : m_ast_macros)
    {
        ogm_ast_free(std::get<1>(pair));
    }

    // delete enums
    assert(m_enums != nullptr);
    delete(m_enums);
}

void ReflectionAccumulator::set_macro(const char* name, const char* value, int flags)
{
    WRITE_LOCK(m_mutex_macros);
    auto& macros = m_ast_macros;
    ogm_ast_t* ast;
    flags |= ogm_ast_parse_flag_no_decorations;
    try
    {
        // try parsing as an expression
        ast = ogm_ast_parse_expression(value, flags);
    }
    catch (...)
    {
        // not a valid expression -- try as a statement instead
        try
        {
            ast = ogm_ast_parse(value, flags);
        }
        catch (...)
        {
            throw ogm::CompileError(ErrorCode::C::parsemacro, "Cannot parse macro \"{}\" as either an expression nor statement: \"{}\"", name, value);
        }
    }

    if (m_ast_macros.find(name) != m_ast_macros.end())
    {
        // overwrite existing macro
        ogm_ast_free(m_ast_macros[name]);
    }
    m_ast_macros[name] = ast;
}

// default implementation of generate_accessor_bytecode.
// Because it is unlikely for most implementations to change this implementation, a default is provided.
bool Library::generate_accessor_bytecode(std::ostream& out, accessor_type_t type, size_t pop_count, bool store) const
{
    switch (type)
    {
        case accessor_map:
            if (pop_count != 2)
            {
                throw CompileError(ErrorCode::C::accmapargs, "map accessor needs exactly 1 argument.");
            }
            if (store)
            {
                bool success = generate_function_bytecode(out, "ds_map_replace", pop_count + 1);
                if (success)
                {
                    // pop undefined value returned by ds_map_set.
                    write_op(out, pop);
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return generate_function_bytecode(out, "ds_map_find_value", pop_count);
            }
            break;
        case accessor_grid:
            if (pop_count != 3)
            {
                throw CompileError(ErrorCode::C::accgridargs, "grid accessor needs exactly 2 arguments.");
            }
            if (store)
            {
                bool success = generate_function_bytecode(out, "ds_grid_set", pop_count + 1);
                if (success)
                {
                    // pop undefined value returned by ds_grid_set.
                    write_op(out, pop);
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return generate_function_bytecode(out, "ds_grid_get", pop_count);
            }
            break;
        case accessor_list:
            if (pop_count != 2)
            {
                throw CompileError(ErrorCode::C::acclistargs, "list accessor needs exactly 1 argument.");
            }
            if (store)
            {
                bool success = generate_function_bytecode(out, "ds_list_set", pop_count + 1);
                if (success)
                {
                    // pop undefined value returned by ds_list_set.
                    write_op(out, pop);
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return generate_function_bytecode(out, "ds_list_find_value", pop_count);
            }
            break;
        default:
            return false;
    }
}

// forward declarations
void bytecode_generate_ast(std::ostream& out, const ogm_ast_t& ast, GenerateContextArgs context_args);

// replaces addresses placeholders in the given vector from the given index onward.
// if k_invalid_pos is passed into the pos value, they will be replaced by the address of their following instruction
void bytecode_replace_placeholder_addresses(std::ostream& out, std::vector<bytecode_address_t>& placeholders, size_t start, bytecode_address_t dst)
{
    while (placeholders.size() > start)
    {
        bytecode_address_t src = placeholders.back();
        placeholders.pop_back();
        bytecode_address_t _dst = dst;
        if (dst == -1)
        {
            _dst = src + sizeof(bytecode_address_t);
        }
        write_at(out, _dst, src);
    }
}

inline void bytecode_generate_pops(std::ostream& out, int16_t pop_count)
{
    for (int16_t i = 0; i < pop_count; i++)
    {
        write_op(out, pop);
    }
}

void preprocess_function_special(const ogm_ast_t& ast)
{
    assert(ast.m_sub_count >= 1);
    if (ast.m_sub[0].m_subtype == ogm_ast_st_exp_identifier)
    {
        const char* function_name = (char*) ast.m_sub[0].m_payload;
        uint8_t argc = ast.m_sub_count - 1;
        
        // TODO
    }
}

bool generate_function_special(std::ostream& out, const ogm_ast_t& ast, GenerateContextArgs context_args)
{
    assert(ast.m_sub_count >= 1);
    if (ast.m_sub[0].m_subtype == ogm_ast_st_exp_identifier)
    {
        const char* function_name = (char*) ast.m_sub[0].m_payload;
        uint8_t argc = ast.m_sub_count - 1;

        if (strcmp(function_name, "gml_pragma") == 0)
        {
            // TODO:
            return true;
        }
        
        if (strcmp(function_name, "gml_release_mode") == 0)
        {
            // TODO:
            return true;
        }

        if (strcmp(function_name, "ogm_pragma") == 0)
        {
            // TODO:
            return true;
        }
        
        if (strcmp(function_name, "ogm_release_mode") == 0)
        {
            // TODO:
            return true;
        }
        
        if (strcmp(function_name, "ogm_volatile") == 0 && argc == 1)
        {
            bytecode_generate_ast(out, ast.m_sub[1], context_args);
            return true;
        }
    }

    return false;
}

void bytecode_generate_ast(std::ostream& out, const ogm_ast_t& ast, GenerateContextArgs context_args)
{
    try
    {
        auto start_location = out.tellp();
        

        switch (ast.m_subtype)
        {
            case ogm_ast_st_exp_literal_primitive:
            {
                ogm_ast_literal_primitive_t* payload;
                ogm_ast_tree_get_payload_literal_primitive(&ast, &payload);
                
                switch (payload->m_type)
                {
                    case ogm_ast_literal_t_number:
                    {
                        char* s = payload->m_value;
                        if (s[0] == '$' || (s[0] == '0' && s[1] == 'x'))
                        {//Parse out a hex string.
                            // hex string
                            uint64_t v = 0;
                            for (size_t i = 1 + (s[0] == '0'); s[i] != 0; i++)
                            {
                                v <<= 4;
                                if (s[i] >= '0' && s[i] <= '9')
                                {
                                    v += s[i] - '0';
                                }
                                else if (s[i] >= 'a' && s[i] <= 'f')
								{
									v += 10 + s[i] - 'a';
								}
								else if (s[i] >= 'A' && s[i] <= 'F')
								{
									v += 10 + s[i] - 'A';
								}
                            }
                            if (v >= 1 << 31)
                            {
                                write_op(out, ldi_u64);
                                write(out, v);
                            }
                            else
                            {
                                write_op(out, ldi_f64);
                                real_t vs = static_cast<real_t>(v);
                                write(out, vs);
                            }
                        }
                        else//Parse out a number string.
                        {
                            // decimal
                            if (char* p = strchr(s, '.'))
                            {
                                double v = 0;
                                size_t mult = 1;
                                for (char* c = p - 1; c >= s; c--)
                                {
                                    v += mult * (*c - '0');
                                    mult *= 10;
                                }
                                double multf = 1;
                                for (size_t i = p - s + 1; s[i] != 0; i++)
                                {
                                    multf *= 0.1;
                                    v += multf * (s[i] - '0');
                                }
                                write_op(out, ldi_f64);
                                write(out, v);
                            }
                            else
                            {
                                // int
                                uint64_t v = 0;
                                for (size_t i = 0; s[i] != 0; i++)
                                {
                                    v *= 10;
                                    v += s[i] - '0';
                                }

                                if (v < (1ULL << 53)) // 53-bit limit of double precision integers
								{
                                    write_op(out, ldi_u64);
                                    write(out, v);
                                }
                                else
                                {
                                    write_op(out, ldi_f64);
                                    real_t vs = static_cast<real_t>(v);
                                    write(out, vs);
                                }
                            }
                        }
                    }
                    break;
                    case ogm_ast_literal_t_string:
                    {
                        write_ldi_string(out, payload->m_value, true);
                    }
                    break;
                }
            }
            break;
            case ogm_ast_st_exp_literal_array:
            {
                write_op(out, ldi_arr);
                // write back-to-front, because that reserves the array.
                for (int32_t i = ast.m_sub_count; i --> 0;)
                {
                    #ifdef OGM_2DARRAY
                    write_op(out, ldi_false); // punning false and 0.
                    #endif
                    write_op(out, ldi_s32);
                    write(out, i);
                    bytecode_generate_ast(out, ast.m_sub[i], context_args);
                    write_op(out, seti);
                }
            }
            break;
            case ogm_ast_st_exp_literal_struct:
            {
                #ifdef OGM_STRUCT_SUPPORT
                ogm_ast_declaration_t* payload;
                ogm_ast_tree_get_payload_declaration(&ast, &payload);
                
                // create an empty struct
                write_op(out, ldi_struct);
                
                // set its initial members
                for (int32_t i = 0; i < ast.m_sub_count; ++i)
                {
                    write_op(out, dup);
                    bytecode_generate_ast(out, ast.m_sub[i], context_args);
                    
                    // determine variable ID from name.
                    LValue lv;
                    lv.m_memspace = memspace_other;
                    
                    lv.m_address = context_args.m_instance_variables.get_id(
                        // the member's name
                        payload->m_identifier[i]
                    );
                    bytecode_generate_store(out, lv, context_args);
                }
                #else
                throw CompileError(ErrorCode::C::hasstruct, ast.m_start, "struct support is not enabled. Please recompile with -DOGM_STRUCT_SUPPORT=ON");
                #endif
            }
            break;
            case ogm_ast_st_exp_literal_function:
            {
                #ifdef OGM_FUNCTION_SUPPORT
                
                // push function onto stack
                write_op(out, ldi_fn);
                
                ogm_ast_literal_function_t* payload;
                ogm_ast_tree_get_payload_function_literal(&ast, &payload);
                
                std::string name = "<lambda-" + std::to_string((*context_args.m_lambda_id)++) + ">";
                if (payload->m_name)
                {
                    name = payload->m_name;
                }
                
                ogm_assert(payload->m_arg_count <= 16);
                
                DecoratedAST dast{
                    // body of function
                    ast.m_sub,
                    context_args.m_dast->m_name + "#" + name,
                    context_args.m_dast->m_source,
                    1, // return count
                    static_cast<uint8_t>(payload->m_arg_count) // arguments
                };
                
                // set named arguments
                if (payload->m_arg_count >= 0)
                {
                    dast.m_named_args = new std::string[payload->m_arg_count];
                    for (int32_t i = 0; i < payload->m_arg_count; ++i)
                    {
                        dast.m_named_args[i] = payload->m_arg[i];
                    }
                }
                
                // remove some config flags that lambdas should ignore.
                GenerateConfig _config = *context_args.m_config;
                _config.m_no_locals = false;
                _config.m_existing_locals_namespace = nullptr;
                _config.m_return_is_suspend = false;
                
                bytecode_index_t lambda_index = bytecode_generate(
                    dast,
                    *context_args.m_accumulator,
                    &_config
                );
                
                write(out, lambda_index);
                
                // TODO: assign to instance variable.
                // (make sure to assert the variable name doesn't already exist.)
                if (ast.m_type == ogm_ast_t_imp)
                {
                    write_op(out, pop);
                }
                
                #else
                throw CompileError(ErrorCode::C::haslitfn, ast.m_start, "function literal support is not enabled. Please recompile with -DOGM_FUNCTION_SUPPORT=ON");
                #endif
            }
            break;
            case ogm_ast_st_exp_ternary_if:
            {
                // condition
                bytecode_generate_ast(out, ast.m_sub[0], context_args);
                write_op(out, ncond);
                write_op(out, bcond);

                //backpatch address later
                bytecode_address_t neg_cond_src = out.tellp();
                bytecode_address_t neg_cond_dst;
                bytecode_address_t body_end_src;
                bytecode_address_t body_end_dst;

                // write a placeholder address, to be filled in later.
                write(out, neg_cond_src);
                peephole_block(out, context_args);
                bytecode_generate_ast(out, ast.m_sub[1], context_args);
                write_op(out, jmp);
                body_end_src = out.tellp();
                write(out, body_end_src);
                neg_cond_dst = out.tellp();
                write_at(out, neg_cond_dst, neg_cond_src);
                peephole_block(out, context_args);
                bytecode_generate_ast(out, ast.m_sub[2], context_args);
                body_end_dst = out.tellp();
                peephole_block(out, context_args);
                write_at(out, body_end_dst, body_end_src);
            }
            break;
            case ogm_ast_st_exp_possessive:
            {
                // check if this is an enum
                if (ast.m_sub[0].m_subtype == ogm_ast_st_exp_identifier)
                {
                    READ_LOCK(context_args.m_reflection->m_mutex_enums);
                    const auto& enum_map = context_args.m_reflection->m_enums->m_map;
                    std::string enum_name = static_cast<const char*>(ast.m_sub[0].m_payload);
                    auto enum_iter = enum_map.find(enum_name);
                    if (enum_iter != enum_map.end())
                    {
                        std::string q = static_cast<const char*>(ast.m_sub[1].m_payload);

                        const std::map<std::string, ogm_ast_t*>& map = std::get<1>(*enum_iter).m_map;
                        const auto& value_iter = map.find(q);
                        if (value_iter == map.end())
                        {
                            throw CompileError(ErrorCode::C::enummissing, ast.m_start, "Enum element \"{}\" is not a member of enum \"{}\"", q, enum_name);
                        }
                        const ogm_ast_t* data = value_iter->second;
                        context_args.m_enum_expression = true;
                        bytecode_generate_ast(out, *data, context_args);
                        break;
                    }
                }
            }
            // fallthrough
            case ogm_ast_st_exp_identifier:
            case ogm_ast_st_exp_accessor:
            case ogm_ast_st_exp_global:
            {
                LValue lv = bytecode_generate_get_lvalue(out, ast, context_args);
                bytecode_generate_load(out, lv, context_args);
            }
            break;
            case ogm_ast_st_imp_assignment:
            {
                const ogm_ast_t& lhs = ast.m_sub[0];
                const ogm_ast_t& rhs = ast.m_sub[1];

                LValue lv = bytecode_generate_get_lvalue(out, lhs, context_args);

                // for relative assignments (+= etc.), need to get value of lhs.
                if (ast.m_spec != ogm_ast_spec_op_eq)
                {
                    bytecode_generate_reuse_lvalue(out, lv);
                    bytecode_generate_load(out, lv, context_args);
                }

                // generate rhs
                bytecode_generate_ast(out, rhs, context_args);

                // relative assignment must retrieve lhs:
                switch (ast.m_spec)
                {
                    case ogm_ast_spec_op_pluseq:
                        write_op(out, add2);
                        break;
                    case ogm_ast_spec_op_minuseq:
                        write_op(out, sub2);
                        break;
                    case ogm_ast_spec_op_timeseq:
                        write_op(out, mult2);
                        break;
                    case ogm_ast_spec_op_divideeq:
                        write_op(out, fdiv2);
                        break;
                    case ogm_ast_spec_op_andeq:
                        write_op(out, band);
                        break;
                    case ogm_ast_spec_op_oreq:
                        write_op(out, bor);
                        break;
                    case ogm_ast_spec_op_xoreq:
                        write_op(out, bxor);
                        break;
                    case ogm_ast_spec_op_leftshifteq:
                        write_op(out, lsh2);
                        break;
                    case ogm_ast_spec_op_rightshifteq:
                        write_op(out, rsh2);
                        break;
                    case ogm_ast_spec_op_modeq:
                        write_op(out, mod2);
                        break;
                    case ogm_ast_spec_op_eq:
                        break;
                    default:
                        throw LanguageFeatureNotImplementedError("Assignment operator not supported.");
                }

                // assign.
                bytecode_generate_store(out, lv, context_args);
            }
            break;
            case ogm_ast_st_exp_arithmetic:
                {
                    bool ignore_result = ast.m_type == ogm_ast_t_imp;
                    switch (ast.m_spec)
                    {
                    case ogm_ast_spec_op_unary_pre_plusplus:
                    case ogm_ast_spec_op_unary_post_plusplus:
                    case ogm_ast_spec_op_unary_pre_minusminus:
                    case ogm_ast_spec_op_unary_post_minusminus:
                        {
                            LValue lv = bytecode_generate_get_lvalue(out, ast.m_sub[0], context_args);
                            bool pre = ast.m_spec == ogm_ast_spec_op_unary_pre_plusplus || ast.m_spec == ogm_ast_spec_op_unary_pre_minusminus;
                            bool plus = ast.m_spec == ogm_ast_spec_op_unary_pre_plusplus || ast.m_spec == ogm_ast_spec_op_unary_post_plusplus;

                            if (lv.m_pop_count == 0)
                            // optimization for memory access opcodes that don't pop references from the stack
                            {
                                if (!pre && !ignore_result)
                                {
                                    bytecode_generate_load(out, lv, context_args);
                                }

                                // increment
                                if (lv.m_memspace == memspace_local)
                                {
                                    // optimization for local variables
                                    write_op(out, plus ? incl : decl)
                                    write(out, lv.m_address);
                                }
                                else
                                {
                                    bytecode_generate_load(out, lv, context_args);
                                    write_op(out, plus ? inc : dec)
                                    bytecode_generate_store(out, lv, context_args);
                                }

                                if (pre && !ignore_result)
                                {
                                    bytecode_generate_load(out, lv, context_args);
                                }
                            }
                            else
                            {
                                if (!ignore_result)
                                {
                                    bytecode_generate_reuse_lvalue(out, lv);
                                }
                                bytecode_generate_reuse_lvalue(out, lv);
                                bytecode_generate_load(out, lv, context_args);
                                write_op(out, plus ? inc : dec)
                                bytecode_generate_store(out, lv, context_args);
                                if (!ignore_result)
                                {
                                    bytecode_generate_load(out, lv, context_args);
                                    if (!pre)
                                    {
                                        // undo the operation
                                        write_op(out, plus ? dec : inc)
                                    }
                                }
                            }

                            ignore_result = false;
                        }
                        break;
                    #ifdef SHORTCIRCUIT_EVALUATIONS
                    case ogm_ast_spec_op_boolean_and:
                    case ogm_ast_spec_op_boolean_and_kw:
                        // short-circuit `and`
                        {
                            // OPTIMIZE: massively optimize shortcircuit evaluations.

                            // left-hand side
                            bytecode_generate_ast(out, ast.m_sub[0], context_args);

                            write_op(out, ncond);
                            write_op(out, bcond);

                            // backpatch later
                            bytecode_address_t shortcircuit_src = out.tellp();
                            write(out, shortcircuit_src);
                            peephole_block(out, context_args);

                            // right-hand side
                            bytecode_generate_ast(out, ast.m_sub[1], context_args);
                            write_op(out, ncond);

                            // backpatch
                            bytecode_address_t loc = out.tellp();
                            write_at(out, loc, shortcircuit_src)
                            peephole_block(out, context_args);

                            // write flipped condition
                            // FIXME: this is hideous.
                            write_op(out, pcond);
                            write_op(out, ncond);
                            write_op(out, pcond);
                        }
                        break;
                    case ogm_ast_spec_op_boolean_or:
                    case ogm_ast_spec_op_boolean_or_kw:
                        // short-circuit `and`
                        {
                            // OPTIMIZE: massively optimize shortcircuit evaluations.

                            // left-hand side
                            bytecode_generate_ast(out, ast.m_sub[0], context_args);

                            write_op(out, cond);
                            write_op(out, bcond);

                            // backpatch later
                            bytecode_address_t shortcircuit_src = out.tellp();
                            write(out, shortcircuit_src);
                            peephole_block(out, context_args);

                            // right-hand side
                            bytecode_generate_ast(out, ast.m_sub[1], context_args);
                            write_op(out, cond);

                            // backpatch
                            bytecode_address_t loc = out.tellp();
                            write_at(out, loc, shortcircuit_src)
                            peephole_block(out, context_args);

                            // write condition
                            write_op(out, pcond);
                        }
                        break;
                    #endif
                    default:
                        bytecode_generate_ast(out, ast.m_sub[0], context_args);
                        if (ast.m_sub_count == 1)
                        // unary operators
                        {
                            switch (ast.m_spec)
                            {
                                case ogm_ast_spec_op_plus:
                                    // unary plus is meaningless.
                                    break;
                                case ogm_ast_spec_op_minus:
                                    // multiply by -1. (OPTIMIZE?)
                                    {
                                        real_t neg = -1;
                                        write_op(out, ldi_f64);
                                        write(out, neg);
                                        write_op(out, mult2);
                                    }
                                    break;
                                case ogm_ast_spec_opun_boolean_not:
                                case ogm_ast_spec_opun_boolean_not_kw:
                                    write_op(out, ncond);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_opun_bitwise_not:
                                    write_op(out, bnot);
                                    break;
                                default:
                                    throw LanguageFeatureNotImplementedError("Unknown unary operator.");
                            }
                        }
                        else
                        // binary operators
                        {
                            // compute right-hand side
                            bytecode_generate_ast(out, ast.m_sub[1], context_args);

                            switch (ast.m_spec)
                            {
                                case ogm_ast_spec_op_plus:
                                    write_op(out, add2);
                                    break;
                                case ogm_ast_spec_op_minus:
                                    write_op(out, sub2);
                                    break;
                                case ogm_ast_spec_op_times:
                                    write_op(out, mult2);
                                    break;
                                case ogm_ast_spec_op_integer_division_kw:
                                    write_op(out, idiv2);
                                    break;
                                case ogm_ast_spec_op_divide:
                                    write_op(out, fdiv2);
                                    break;
                                case ogm_ast_spec_op_mod:
                                case ogm_ast_spec_op_mod_kw:
                                    write_op(out, mod2);
                                    break;
                                #ifndef SHORTCIRCUIT_EVALUATIONS
                                case ogm_ast_spec_op_boolean_and:
                                case ogm_ast_spec_op_boolean_and_kw:
                                    write_op(out, bland);
                                    break;
                                case ogm_ast_spec_op_boolean_or:
                                case ogm_ast_spec_op_boolean_or_kw:
                                    write_op(out, blor);
                                    break;
                                #endif
                                case ogm_ast_spec_op_boolean_xor:
                                case ogm_ast_spec_op_boolean_xor_kw:
                                    write_op(out, blxor);
                                    break;
                                case ogm_ast_spec_op_bitwise_and:
                                    write_op(out, band);
                                    break;
                                case ogm_ast_spec_op_bitwise_or:
                                    write_op(out, bor);
                                    break;
                                case ogm_ast_spec_op_bitwise_xor:
                                    write_op(out, bxor);
                                    break;
                                case ogm_ast_spec_op_leftshift:
                                    write_op(out, lsh2);
                                    break;
                                case ogm_ast_spec_op_rightshift:
                                    write_op(out, rsh2);
                                    break;
                                case ogm_ast_spec_op_eq:
                                case ogm_ast_spec_op_eqeq:
                                    write_op(out, eq);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_op_lt:
                                    write_op(out, lt);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_op_gt:
                                    write_op(out, gt);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_op_lte:
                                    write_op(out, lte);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_op_gte:
                                    write_op(out, gte);
                                    write_op(out, pcond);
                                    break;
                                case ogm_ast_spec_op_neq:
                                case ogm_ast_spec_op_ltgt:
                                    write_op(out, neq);
                                    write_op(out, pcond);
                                    break;
                                default:
                                    throw LanguageFeatureNotImplementedError("Unknown binary operator.");
                            }
                        }
                    }

                    // statements ignore result.
                    if (ignore_result)
                    {
                        write_op(out, pop);
                    }
                }
                break;
            case ogm_ast_st_exp_paren:
                bytecode_generate_ast(out, ast.m_sub[0], context_args);
                break;
            case ogm_ast_st_exp_fn:
            case ogm_ast_st_exp_new:
                {
                    if (generate_function_special(out, ast, context_args))
                    {
                        break;
                    }
                    
                    const bool is_new = ast.m_subtype == ogm_ast_st_exp_new;
                    
                    if (is_new)
                    {
                        #if OGM_STRUCT_SUPPORT
                        write_op(out, ldi_struct);
                        if (ast.m_type == ogm_ast_t_exp)
                        {
                            // we need to save a copy of the struct to return from the expression.
                            write_op(out, dup);
                        }
                        write_op(out, wti);
                        #else
                        CompileError(ErrorCode::C::hasstruct, ast.m_start, "\"new\" keyword requires struct support. Please recompile with -DOGM_STRUCT_SUPPORT.");
                        #endif
                    }
                    
                    assert(ast.m_sub_count >= 1);
                    
                    uint8_t retc = 0;
                    uint8_t argc = ast.m_sub_count - 1;

                    // generate arg bytecode in forward order
                    for (size_t i = 0; i < argc; i++)
                    {
                        bytecode_generate_ast(out, ast.m_sub[i + 1], context_args);
                    }
                    
                    auto struct_context = [is_new, &out](uint8_t context_depth)
                    {
                        // switch to context of new struct.
                        if (is_new)
                        {
                            // copy with-iterator
                            write_op(out, dupi);
                            write_op(out, context_depth);
                            write_op(out, wty);
                            // TODO: assert wty sets cond to false?
                            // remote with-iterator from stack
                            write_op(out, pop);
                        }
                    };
                    
                    // figure out what function is being called.
                    if (ast.m_sub[0].m_subtype == ogm_ast_st_exp_identifier)
                    {
                        const char* function_name = (char*) ast.m_sub[0].m_payload;
                        
                        size_t macro_iters = 0;
                        while (context_args.m_reflection->has_macro_NOMUTEX(function_name))
                        // macro?
                        {
                            // swap out function name for macro.
                            ogm_ast_t* macro = context_args.m_reflection->m_ast_macros.at(function_name);
                            if (macro->m_subtype == ogm_ast_st_exp_identifier)
                            {
                                function_name = ogm_ast_tree_get_payload_string(macro);
                            }
                            else
                            {
                                CompileError(ErrorCode::C::fnmacid, ast.m_start, "function identifier matched a macro which did not evaluate to an identifier.");
                            }
                            
                            if (macro_iters++ > 0x500)
                            {
                                CompileError(ErrorCode::C::macdepth, ast.m_start, "macro replacement depth exceeded (for function identifier).");
                            }
                        }
                        
                        if (context_args.m_library->generate_function_bytecode(out, function_name, argc))
                        // library function
                        {
                            retc = 1;
                        }
                        else
                        // function name not found in library
                        {
                            // TODO: check extensions

                            // check scripts
                            asset_index_t asset_index;
                            if (const AssetScript* script = dynamic_cast<const AssetScript*>(context_args.m_asset_table->get_asset(function_name, asset_index)))
                            {
                                const Bytecode bytecode = context_args.m_bytecode_table->get_bytecode(script->m_bytecode_index);

                                struct_context(argc);
                                write_op(out, call);
                                write(out, script->m_bytecode_index);
                                retc = bytecode.m_retc;
                                if (bytecode.m_argc != static_cast<uint8_t>(-1) && argc != bytecode.m_argc)
                                {
                                    throw CompileError(ErrorCode::C::scrargs, ast.m_start,
                                        "Wrong number of arguments to script \"{}\"; expected {}, provided {}.",
                                        function_name, bytecode.m_argc, argc
                                    );
                                }
                                write(out, argc);
                            }
                            else
                            {
                                #ifdef OGM_FUNCTION_SUPPORT
                                goto indirect_function;
                                #else
                                throw CompileError(ErrorCode::C::unkfnscr, ast.m_start, "Unknown function or script: {} (with {} arguments provided)", function_name, npluralize("argument", argc));
                                #endif
                            }
                        }
                    }
                    else
                    {
                        #ifdef OGM_FUNCTION_SUPPORT
                    indirect_function:
                        retc = 1;
                        bytecode_generate_ast(out, ast.m_sub[0], context_args);
                        if (is_new)
                        {
                            // set self to the struct
                            struct_context(argc + 1);
                            
                            // copy the function on the stack
                            write_op(out, dup);
                            
                            // set the struct-type to the function
                            write_op(out, tstruct);
                        }
                        write_op(out, calls);
                        write(out, argc);
                        #else
                        throw CompileError(ErrorCode::C::haslitfn, ast.m_start, "To access arbitrary values as functions, please recompile with -DOGM_FUNCTION_SUPPORT.");
                        #endif
                    }

                    if (ast.m_type == ogm_ast_t_imp || is_new)
                    // ignore output value
                    {
                        for (size_t i = 0; i < retc; ++i)
                        {
                            write_op(out, pop);
                        }
                    }
                    
                    if (is_new)
                    {
                        // restore context.
                        write_op(out, wtd);
                    }
                }
                break;
            case ogm_ast_st_imp_var:
                {
                    ogm_ast_declaration_t* payload;
                    ogm_ast_tree_get_payload_declaration(&ast, &payload);

                    LValue type_lv;
                    type_lv.m_memspace = memspace_local;

                    const char* dectype = "";

                    // add variable names to local namespace and assign values if needed.
                    for (size_t i = 0; i < payload->m_identifier_count; i++)
                    {
                        // update type for this iteration if set:
                        if (payload->m_types[i])
                        {
                            dectype = payload->m_types[i];
                        }
                        
                        // select type
                        bool is_static = false;
                        bool is_bare = false;
                        if (strcmp(dectype, "globalvar") == 0)
                        {
                            type_lv.m_memspace = memspace_global;
                            is_bare = true;
                        }
                        else if (strcmp(dectype, "static") == 0)
                        {
                            type_lv.m_memspace = memspace_global;
                            is_static = true;
                        }
                        else
                        {
                            type_lv.m_memspace = memspace_local;
                        }
                        
                        // lvalue
                        const char* identifier = payload->m_identifier[i];
                        if (!identifier) continue;
                        
                        LValue lv = type_lv;
                        if (lv.m_memspace == memspace_local)
                        {
                            // local variable
                            lv.m_address = context_args.m_symbols->m_namespace_local.add_id(identifier);
                        }
                        else if (lv.m_memspace == memspace_global)
                        {
                            // globalvar
                            std::string _identifier = identifier;
                            if (is_static)
                            {
                                _identifier = context_args.m_dast->m_name + "::" + identifier;
                            }
                            
                            // set address
                            lv.m_address = context_args.m_reflection->m_namespace_instance.add_id(_identifier);
                            
                            if (is_static)
                            {
                                // remember static for context later.
                                (*context_args.m_statics)[identifier] = lv.m_address;
                                
                                // so we can later remap from instance variable to the static variable.
                                variable_id_t nonstatic_id = context_args.m_reflection->m_namespace_instance.add_id(identifier);
                                context_args.m_accumulator->m_bytecode->add_static(
                                    context_args.m_bytecode_index, nonstatic_id, lv.m_address
                                );
                            }

                            // mark as a "bare" global, meaning it can be referenced without the `global.` prefix.
                            if (is_bare)
                            {
                                WRITE_LOCK(context_args.m_reflection->m_mutex_bare_globals);
                                context_args.m_reflection->m_bare_globals.insert(_identifier);
                            }
                        }
                        
                        bytecode_address_t static_backpatch_src;
                        
                        if (is_static)
                        {
                            // static initialization
                            write_op(out, okg);
                            write(out, lv.m_address);
                            write_op(out, bcond);
                            
                            // dummy value to be backpatched.
                            static_backpatch_src = out.tellp();
                            write(out, static_backpatch_src);
                            peephole_block(out, context_args);
                        }

                        if (ast.m_sub[i].m_subtype != ogm_ast_st_imp_empty)
                        // some declarations have definitions, too.
                        {
                            // calculate assignment and give value.
                            bytecode_generate_ast(out, ast.m_sub[i], context_args);
                        }
                        else
                        {
                            // defaults to "undefined"
                            write_op(out, ldi_undef);
                        }
                        
                        // store value in var definition.
                        bytecode_generate_store(out, lv, context_args);
                        
                        // backpatch for statics
                        if (is_static)
                        {
                            bytecode_address_t static_backpatch_dst = out.tellp();
                            write_at(out, static_backpatch_dst, static_backpatch_src);
                            peephole_block(out, context_args);
                        }
                    }
                }
                break;
            case ogm_ast_st_imp_enum:
                // enums only matter during preprocessing.
                break;
            case ogm_ast_st_imp_empty:
                break;
            case ogm_ast_st_imp_body:
                for (size_t i = 0; i < ast.m_sub_count; i++)
                {
                    bytecode_generate_ast(out, ast.m_sub[i], context_args);
                }
                break;
            case ogm_ast_st_imp_if:
                {
                    // In an effort to avoid stack overflows while compiling
                    // due to long if-else chains (e.g. 5000 subsequent "else if"s),
                    // we collect subsequent if-elses here.
                    std::vector<const ogm_ast_t*> conditions;
                    std::vector<const ogm_ast_t*> bodies;
                    const ogm_ast_t* collect_from = &ast;
                    
                    // Descend into the last "else" block each time.
                    while (collect_from)
                    {
                        conditions.push_back(&collect_from->m_sub[0]);
                        
                        // if more conditions are added to "if" asts, this will need to be modified.
                        for (size_t i = 1; i < collect_from->m_sub_count; ++i)
                        {
                            bodies.push_back(&collect_from->m_sub[i]);
                        }
                        
                        if (collect_from->m_sub_count >= 3 && bodies.back()->m_subtype == ogm_ast_st_imp_if)
                        // there is an else -- pop it and iterate over it.
                        {
                            collect_from = bodies.back();
                            bodies.pop_back();
                        }
                        else
                        {
                            // no "else"; we're done collecting.
                            collect_from = nullptr;
                        }
                    }
            
                    // these indices will have the end-of-chain address backpatched onto them.
                    std::vector<bytecode_address_t> backpatch_sources;
                    
                    for (size_t i = 0; i < bodies.size(); ++i)
                    {
                        const bool has_condition = i < conditions.size();
                        const bool is_last_block = i == bodies.size() - 1;
                        bytecode_address_t skip_block_src;
                        
                        // compute condition:
                        if (has_condition)
                        {
                            bytecode_generate_ast(out, *conditions.at(i), context_args);

                            // jump to "else" block if condition is false
                            write_op(out, ncond);
                            write_op(out, bcond);
                            
                            // placeholder
                            skip_block_src = out.tellp();
                            write(out, skip_block_src);
                            peephole_block(out, context_args);
                        }
                        
                        // body
                        bytecode_generate_ast(out, *bodies.at(i), context_args);
                        
                        if (!is_last_block)
                        {
                            // skip to end of if-else block
                            write_op(out, jmp);
                            
                            // write dummy jump dst and backpatch later.
                            bytecode_address_t backpatch_src = out.tellp();
                            backpatch_sources.push_back(backpatch_src);
                            write(out, backpatch_src);
                            peephole_block(out, context_args);
                        }
                        
                        // backpatch
                        if (has_condition)
                        {
                            bytecode_address_t skip_block_dst = out.tellp();
                            write_at(out, skip_block_dst, skip_block_src);
                            peephole_block(out, context_args);
                        }
                    }
                    
                    // backpatch jump from end of each block
                    bytecode_address_t backpatch_dst = out.tellp();
                    for (bytecode_address_t backpatch_src : backpatch_sources)
                    {
                        write_at(out, backpatch_dst, backpatch_src);
                    }
                    peephole_block(out, context_args);
                }
                break;
                case ogm_ast_st_imp_for:
                    {
                        context_args.m_continue_pop_count = 0;
                        const ogm_ast_t& init = ast.m_sub[0];
                        const ogm_ast_t& condition = ast.m_sub[1];
                        const ogm_ast_t& post = ast.m_sub[2];
                        const ogm_ast_t& body = ast.m_sub[3];
                        bytecode_generate_ast(out, init, context_args);

                        bytecode_address_t loop_start = out.tellp();
                        peephole_block(out, context_args);
                        bytecode_generate_ast(out, condition, context_args);
                        bytecode_address_t loop_end_dst;
                        write_op(out, ncond);
                        write_op(out, bcond);
                        
                        // placeholder, to be filled in later.
                        bytecode_address_t loop_end_src = out.tellp();
                        write(out, k_placeholder_pos);

                        // remember where the continue placeholder list was to start.
                        size_t start_continue_replace = context_args.m_continue_placeholders.size();
                        size_t start_break_replace = context_args.m_break_placeholders.size();
                        bytecode_generate_ast(out, body, context_args);
                        bytecode_address_t continue_address = out.tellp();
                        bytecode_generate_ast(out, post, context_args);

                        // jump to start of loop
                        write_op(out, jmp);
                        write(out, loop_start);

                        // replace break/continue addresses within loop body.
                        bytecode_address_t break_address = out.tellp();
                        bytecode_replace_placeholder_addresses(out, context_args.m_continue_placeholders, start_continue_replace, continue_address);
                        bytecode_replace_placeholder_addresses(out, context_args.m_break_placeholders, start_break_replace, break_address);

                        // end of loop
                        loop_end_dst = out.tellp();
                        write_at(out, loop_end_dst, loop_end_src);
                }
                break;
            case ogm_ast_st_imp_loop:
                {
                    context_args.m_continue_pop_count = 0;
                    const ogm_ast_t& condition = ast.m_sub[0];
                    const ogm_ast_t& body = ast.m_sub[1];
                    size_t start_continue_replace = context_args.m_continue_placeholders.size();
                    size_t start_break_replace = context_args.m_break_placeholders.size();
                    bytecode_address_t continue_address;
                    bytecode_address_t break_address;
                    switch (ast.m_spec)
                    {
                        case ogm_ast_spec_loop_repeat:
                            {
                                // store repeat counter on stack anonymously.
                                bytecode_generate_ast(out, condition, context_args);
                                bytecode_address_t loop_start = out.tellp();
                                // check repeat counter
                                write_op(out, dup);
                                write_op(out, ncond);
                                write_op(out, bcond);

                                context_args.m_cleanup_commands.push_back(pop);
                                // placeholder, to be filled in later.
                                bytecode_address_t loop_end_src = out.tellp();
                                write(out, k_placeholder_pos);
                                bytecode_generate_ast(out, body, context_args);

                                continue_address = out.tellp();

                                // decrement loop counter, jump to start of loop
                                write_op(out, dec);
                                write_op(out, jmp);
                                write(out, loop_start);

                                // end of loop
                                break_address = out.tellp();
                                write_at(out, break_address, loop_end_src);
                                write_op(out, pop);

                                context_args.m_cleanup_commands.pop_back();
                            }
                            break;
                        case ogm_ast_spec_loop_while:
                            {
                                bytecode_address_t loop_start = out.tellp();
                                continue_address = out.tellp();
                                bytecode_generate_ast(out, condition, context_args);
                                bytecode_address_t loop_end_dst;
                                write_op(out, ncond);
                                write_op(out, bcond);
                                // placeholder, to be filled in later.
                                bytecode_address_t loop_end_src = out.tellp();
                                write(out, k_placeholder_pos);
                                bytecode_generate_ast(out, body, context_args);

                                // jump to start of loop
                                write_op(out, jmp);
                                write(out, loop_start);

                                // end of loop
                                loop_end_dst = out.tellp();
                                break_address = out.tellp();
                                write_at(out, loop_end_dst, loop_end_src);
                            }
                            break;
                        case ogm_ast_spec_loop_do_until:
                            {
                                bytecode_address_t loop_start = out.tellp();
                                bytecode_generate_ast(out, body, context_args);
                                continue_address = out.tellp();
                                bytecode_generate_ast(out, condition, context_args);
                                write_op(out, ncond);
                                write_op(out, bcond);
                                write(out, loop_start);
                                break_address = out.tellp();
                            }
                            break;
                        case ogm_ast_spec_loop_with:
                            {
                                bytecode_generate_ast(out, condition, context_args);
                                write_op(out, wti);
                                bytecode_address_t loop_start = out.tellp();
                                continue_address = out.tellp();
                                write_op(out, wty);
                                write_op(out, bcond);

                                context_args.m_cleanup_commands.push_back(wtd);

                                bytecode_address_t loop_end_src = out.tellp();
                                bytecode_address_t loop_end_dst;
                                write(out, k_placeholder_pos);
                                bytecode_generate_ast(out, body, context_args);

                                // jump to start of loop
                                write_op(out, jmp);
                                write(out, loop_start);

                                // place a wtd instruction below the loop which
                                // any break instruction will jump to.
                                break_address = out.tellp();
                                if (context_args.m_break_placeholders.size() > start_break_replace)
                                {
                                    write_op(out, wtd);
                                }

                                context_args.m_cleanup_commands.pop_back();

                                // end of loop
                                loop_end_dst = out.tellp();
                                write_at(out, loop_end_dst, loop_end_src);
                            }
                            break;
                        default:
                            throw LanguageFeatureNotImplementedError("Loop type not supported.");
                    }
                    bytecode_replace_placeholder_addresses(out, context_args.m_continue_placeholders, start_continue_replace, continue_address);
                    bytecode_replace_placeholder_addresses(out, context_args.m_break_placeholders, start_break_replace, break_address);
                }
                break;
            case ogm_ast_st_imp_switch:
                {
                    // this could be heavily optimized with e.g. a binary search or indirect jumps.
                    bytecode_generate_ast(out, ast.m_sub[0], context_args);

                    if (ast.m_sub_count > 0)
                    {
                        context_args.m_continue_pop_count++;
                        context_args.m_cleanup_commands.push_back(pop);
                        size_t start_break_replace = context_args.m_break_placeholders.size();
                        size_t default_case_index = ast.m_sub_count;
                        std::vector<bytecode_address_t> label_src(ast.m_sub_count + 1);
                        for (size_t i = 1; i < ast.m_sub_count; i += 2)
                        {
                            size_t case_i = i;

                            if (ast.m_sub[case_i].m_subtype == ogm_ast_st_imp_empty)
                            // default
                            {
                                // handle the default later.
                                if (default_case_index != ast.m_sub_count)
                                {
                                    throw CompileError(ErrorCode::C::switch1def, ast.m_start, "Expected at most one \"default\" label.");
                                }
                                default_case_index = case_i;
                                continue;
                            }
                            else
                            {
                                // case
                                write_op(out, dup);
                                bytecode_generate_ast(out, ast.m_sub[case_i], context_args);
                                write_op(out, eq);
                                write_op(out, bcond);
                                label_src[i >> 1] = out.tellp();
                                write(out, k_placeholder_pos);
                                // handle body later
                            }
                        }

                        // jump to default
                        write_op(out, jmp);
                        label_src[default_case_index >> 1] = out.tellp();
                        write(out, k_placeholder_pos);

                        // make bodies
                        for (size_t i = 1; i < ast.m_sub_count; i += 2)
                        {
                            size_t body_i = i + 1;
                            bytecode_address_t label_dst = out.tellp();
                            write_at(out, label_dst, label_src[i >> 1]);
                            bytecode_generate_ast(out, ast.m_sub[body_i], context_args);
                        }

                        bytecode_address_t end_dst = out.tellp();
                        bytecode_replace_placeholder_addresses(out, context_args.m_break_placeholders, start_break_replace, end_dst);
                        if (default_case_index == ast.m_sub_count)
                        // no default statement
                        {
                            // default 'default' jump
                            write_at(out, end_dst, label_src[default_case_index >> 1]);
                        }
                        context_args.m_continue_pop_count--;
                        context_args.m_cleanup_commands.pop_back();
                    }

                    // end of switch
                    write_op(out, pop);
                }
                break;
            case ogm_ast_st_imp_control:
                switch(ast.m_spec)
                {
                    case ogm_ast_spec_control_exit:
                        // pop temporary values from stack
                        for (auto it = context_args.m_cleanup_commands.rbegin(); it != context_args.m_cleanup_commands.rend(); ++it)
                        {
                            write_op(out, *it);
                        }

                        // undefined return value
                        for (size_t i = 0; i < context_args.m_retc; i++)
                        {
                            write_op(out, ldi_zero);
                        }

                        // bytecode for ret or sus
                        if (context_args.m_config->m_return_is_suspend)
                        {
                            write_op(out, sus);
                        }
                        else
                        {
                            write_op(out, ret);
                            write(out, context_args.m_retc);
                        }
                        break;
                    case ogm_ast_spec_control_return:
                        if (ast.m_sub_count == 0)
                        {
                            throw CompileError(ErrorCode::C::retval, ast.m_start, "return requires return value. (Use \"exit\" if no return value.)");
                        }

                        // pop temporary values from stack
                        // wtd requires special workarounds, as it changes the
                        // context of the return values.
                        {
                            bool has_wtd = false;

                            for (opcode::opcode_t op : context_args.m_cleanup_commands)
                            {
                                if (op == opcode::wtd)
                                {
                                    has_wtd = true;
                                    break;
                                }
                            }

                            if (!has_wtd)
                            {
                                // as for exit above
                                for (auto it = context_args.m_cleanup_commands.rbegin(); it != context_args.m_cleanup_commands.rend(); ++it)
                                {
                                    write_op(out, *it);
                                }

                                // return values
                                for (size_t i = 0; i < ast.m_sub_count; i++)
                                {
                                    bytecode_generate_ast(out, ast.m_sub[i], context_args);
                                }
                            }
                            else
                            {
                                // this is an ugly hack to deal with wtd's context-change.
                                // it's also inefficient.
                                // OPTIMIZE: consider better options for dealing with this.

                                // wtd affects context, so we need to calculate RVs
                                // advance of the wtd op.

                                ogm_assert(ast.m_sub_count < 253); // hack requires deli: retc + 2.

                                for (size_t i = 0; i < ast.m_sub_count; i++)
                                {
                                    bytecode_generate_ast(out, ast.m_sub[i], context_args);
                                }

                                // cleanup ops
                                for (auto it = context_args.m_cleanup_commands.rbegin(); it != context_args.m_cleanup_commands.rend(); ++it)
                                {
                                    opcode::opcode_t op = *it;
                                    
                                    // swap down the one and only return value.
                                    if (op == opcode::pop && ast.m_sub_count == 1)
                                    {
                                        write_op(out, swap);
                                    }
                                    else
                                    {
                                        size_t pops = 1;
                                        if (op == opcode::wtd)
                                            pops = 2;

                                        // duplicate values to work around the fact that
                                        // we don't have arbitrary swapi opcodes.
                                        for (size_t i = 0; i < pops; ++i)
                                        {
                                            uint8_t c;
                                            write_op(out, dupi);
                                            c = ast.m_sub_count + pops - 1;
                                            write(out, c);
                                            write_op(out, deli);
                                            c++;
                                            write(out, c);
                                        }
                                    }

                                    // clean up.
                                    write_op(out, op);
                                }
                            }
                        }

                        // pad remaining return values
                        for (size_t i = ast.m_sub_count; i < context_args.m_retc; i++)
                        {
                            write_op(out, ldi_zero);
                        }

                        // pop extra return values
                        for (size_t i = context_args.m_retc; i < ast.m_sub_count; ++i)
                        {
                            write_op(out, pop);
                        }

                        // bytecode for ret or sus
                        if (context_args.m_config->m_return_is_suspend)
                        {
                            write_op(out, sus);
                        }
                        else
                        {
                            write_op(out, ret);
                            write(out, context_args.m_retc);
                        }
                        break;
                    case ogm_ast_spec_control_break:
                        write_op(out, jmp);
                        context_args.m_break_placeholders.push_back(out.tellp());
                        write(out, k_invalid_pos);
                        break;
                    case ogm_ast_spec_control_continue:
                        // pop commands required by switch
                        bytecode_generate_pops(out, context_args.m_continue_pop_count);

                        // jump to placeholder value
                        write_op(out, jmp);
                        context_args.m_continue_placeholders.push_back(out.tellp());
                        write(out, k_invalid_pos);
                        break;
                }
                break;
            case ogm_ast_st_imp_macro_def:
                // handled during preprocessing instead
                break;
            default:
                throw CompileError(ErrorCode::C::stunexpect, ast.m_start, "can't handle {} here", ogm_ast_subtype_string[ast.m_subtype]);
        }

        context_args.m_symbols->m_source_map.add_location(start_location, out.tellp(), ast.m_start, ast.m_end, ast.m_type == ogm_ast_t_imp);
    }
    catch (ogm::Error& error)
    {
        throw error
            .detail<location_start>(ast.m_start)
            .detail<location_end>(ast.m_end)
            .detail<source_buffer>(context_args.m_symbols->m_source);
    }
}

bytecode_index_t bytecode_generate(const DecoratedAST& in, ProjectAccumulator& accumulator, GenerateConfig* config, bytecode_index_t index)
{
    // args (reassign to variables to fit with legacy code... FIXME clean this up eventually.)
    if (!in.m_ast) throw MiscError("AST required to generate bytecode.");
    const ogm_ast_t& ast = *in.m_ast;
    uint8_t retc = in.m_retc, argc = in.m_argc;
    std::string debugSymbolName = in.m_name;
    std::string debugSymbolSource = in.m_source;
    ReflectionAccumulator* reflectionAccumulator;
    const asset::AssetTable* assetTable;
    BytecodeTable* bytecodeTable;

    GenerateConfig defaultConfig;
    bytecodeTable = accumulator.m_bytecode;
    assetTable = accumulator.m_assets;
    reflectionAccumulator = accumulator.m_reflection;

    if (!config)
    {
        config = &defaultConfig;
    }

    bytecode::DebugSymbols* debugSymbols = new bytecode::DebugSymbols(debugSymbolName, debugSymbolSource);

    if (config->m_existing_locals_namespace)
    // pre-populate locals (copy the existing locals)
    {
        debugSymbols->m_namespace_local = *config->m_existing_locals_namespace;
    }

    std::vector<bytecode_address_t> ph_break;
    std::vector<bytecode_address_t> ph_continue;
    std::vector<opcode::opcode_t> cleanup_commands;
    
    // set index if not provided.
    if (index == k_no_bytecode)
    {
        index = accumulator.next_bytecode_index();
    }

    bytecode_address_t peephole_block = 0;
    GenerateContextArgs context_args(
        retc, argc,
        &accumulator,
        &in,
        index,
        reflectionAccumulator->m_namespace_instance,
        reflectionAccumulator->m_namespace_instance,
        accumulator.m_library, assetTable, bytecodeTable,
        ph_break, ph_continue, cleanup_commands,
        debugSymbols, reflectionAccumulator,
        config, 
        peephole_block
    );

    std::stringstream out;

    // cast off outer body_container ast.
    const ogm_ast_t* generate_from_ast = &ast;
    if (ast.m_subtype == ogm_ast_st_imp_body_list)
    {
        if (ast.m_sub_count == 0)
        {
            generate_from_ast = nullptr;
        }
        else if (ast.m_sub_count == 1)
        {
            generate_from_ast = &ast.m_sub[0];
        }
        else
        {
            throw MiscError("More than one body_container provided in ast. This should be handled with multiple calls to bytecode_generate.");
        }
    }

    // allocate local variables
    // TODO: don't allocate locals if no locals in function.
    write_op(out, all);
    int32_t n_locals = 0;
    auto n_locals_src = out.tellp();

    //placeholder -- we won't know the number of locals until after compiling.
    write(out, n_locals);
    
    // copy named args into local variables.
    std::string* named_args = in.m_named_args;
    if (named_args)
    {
        for (int32_t i = 0; i < in.m_argc; ++i)
        {
            LValue lv;
            lv.m_memspace = memspace_local;
            lv.m_address = context_args.m_symbols->m_namespace_local.add_id(named_args[i]);
            
            // get variable definition from library for "argument<n>"
            std::string argname = "argument" + std::to_string(i);
            BuiltInVariableDefinition def;
            if (context_args.m_library->variable_definition(argname.c_str(), def))
            {
                // store argument in local.
                context_args.m_library->generate_variable_bytecode(out, def.m_address, 0, false);
                bytecode_generate_store(out, lv, context_args);
            }
            else
            {
                throw MiscError("Failed to generate implicit argument access for " + argname);
            }
        }
    }
    
    peephole_optimize(out, context_args);

    if (generate_from_ast)
    {
        bytecode_generate_ast(out, *generate_from_ast, context_args);
    }

    // fix any top-level break/continue statements.
    bytecode_replace_placeholder_addresses(out, context_args.m_continue_placeholders, 0, k_invalid_pos);
    bytecode_replace_placeholder_addresses(out, context_args.m_break_placeholders,    0, k_invalid_pos);

    // set number of locals at start of bytecode
    n_locals = context_args.m_symbols->m_namespace_local.id_count();
    write_at(out, n_locals, n_locals_src);

    if (n_locals > 0 && config->m_no_locals)
    {
        throw MiscError("Locals required not to be used, but locals were used anyway.");
    }

    // default return, if no other return was hit.
    for (size_t i = 0; i < retc; i++)
    {
        write_op(out, ldi_zero);
    }
    if (config->m_return_is_suspend)
    {
        write_op(out, sus);
    }
    else
    {
        write_op(out, ret);
        write(out, retc);
    }
    write_op(out, eof);
    std::string s = out.str();
    
    // add generated bytecode to table.
    bytecodeTable->add_bytecode(index, Bytecode(s.c_str(), out.tellp(), retc, argc, debugSymbols));
    
    // return index in table of the bytecode.
    return index;
}

namespace
{
    void preprocess_macro_def(const ogm_ast_macro_def_t* macro_def, ReflectionAccumulator& io_reflection, const Config* config)
    {
        if (macro_def->m_name && macro_def->m_name[0])
        {
            if (macro_def->m_config == nullptr || macro_def->m_config[0] == 0 || (config && config->m_configuration_name == macro_def->m_config))
            {
                io_reflection.set_macro(
                    macro_def->m_name ? macro_def->m_name : "",
                    macro_def->m_value ? macro_def->m_value : "",
                    config ? config->m_parse_flags : 0
                );
            }
        }
    }
    
    void bytecode_preprocess_helper(const ogm_ast_t* ast, const ogm_ast_t* parent, uint8_t& out_retc, uint8_t& out_argc, ReflectionAccumulator& in_out_reflection_accumulator, asset::Config* config)
    {
        try
        {
            // argument style (argument[0] vs argument0)
            if (ast->m_subtype == ogm_ast_st_exp_identifier)
            // check if identifier is an argument
            {
                const char* charv = static_cast<char*>(ast->m_payload);
                const size_t argstrl = strlen("argument");
                if (strlen(charv) > argstrl && memcmp(charv, "argument", argstrl) == 0)
                {
                    // remaining characters are all digits
                    if (is_digits(charv + argstrl))
                    {
                        // get digit value
                        const size_t argn = std::atoi(charv + argstrl);
                        if (argn < 16)
                        // only argument0...argument15 are keywords
                        {
                            if (out_argc == static_cast<uint8_t>(-1) || out_argc < argn + 1)
                            {
                                out_argc = argn + 1;
                            }
                        }
                    }
                }
            }
            
            // macro definition
            // TODO: macros actually need to be pre-preprocessed (!) so that the preprocessing step
            // is able to look up macros. :( ayaaaah....
            if (ast->m_subtype == ogm_ast_st_imp_macro_def)
            {
                const ogm_ast_macro_def_t* macro_def;
                if (ogm_ast_tree_get_payload_macro_def(ast, &macro_def))
                {
                    preprocess_macro_def(macro_def, in_out_reflection_accumulator, config);
                }
            }

            // special functions
            if (ast->m_subtype == ogm_ast_st_exp_fn)
            {
                preprocess_function_special(*ast);
            }

            // retc
            if (ast->m_subtype == ogm_ast_st_imp_control && ast->m_spec == ogm_ast_spec_control_return)
            {
                out_retc = std::max(out_retc, (uint8_t)ast->m_sub_count);
            }

            // globalvar
            if (ast->m_subtype == ogm_ast_st_imp_var)
            {
                ogm_ast_declaration_t* declaration;
                ogm_ast_tree_get_payload_declaration(ast, &declaration);
                const char* dectype = "";
                for (size_t i = 0; i < declaration->m_identifier_count; ++i)
                {
                    if (declaration->m_types[i])
                    {
                        dectype = declaration->m_types[i];
                    }
                    if (strcmp(dectype, "globalvar") == 0)
                    {
                        const char* identifier = declaration->m_identifier[i];
                        if (identifier)
                        {
                            // add the variable name to the list of globals
                            in_out_reflection_accumulator.m_namespace_instance.add_id(identifier);

                            // mark the variable name as a *bare* global specifically
                            // (this means it can be accessed without the `global.` prefix)
                            WRITE_LOCK(in_out_reflection_accumulator.m_mutex_bare_globals);
                            in_out_reflection_accumulator.m_bare_globals.insert(identifier);
                        }
                    }
                }
            }

            // enum
            if (ast->m_subtype == ogm_ast_st_imp_enum)
            {
                WRITE_LOCK(in_out_reflection_accumulator.m_mutex_enums);
                auto& map = in_out_reflection_accumulator.m_enums->m_map;
                ogm_ast_declaration_t* payload;
                ogm_ast_tree_get_payload_declaration(ast, &payload);
                std::string enum_name = payload->m_type;
                if (map.find(enum_name) == map.end())
                {
                    auto& enum_data = map[enum_name];
                    for (size_t i = 0; i < payload->m_identifier_count; ++i)
                    {
                        if (enum_data.m_map.find(payload->m_identifier[i]) != enum_data.m_map.end())
                        {
                            throw CompileError(ErrorCode::C::enumrep, ast->m_start, "Multiple definitions of enum value {}", payload->m_identifier[i]);
                        }

                        if (ast->m_sub[i].m_subtype != ogm_ast_st_imp_empty)
                        // some enum value declarations have definitions, too.
                        {
                            enum_data.m_map[payload->m_identifier[i]] = ogm_ast_copy(&ast->m_sub[i]);
                        }
                        else
                        // no enum value definition
                        {
                            // _ast: will be deleted by enum_data on destruction.
                            ogm_ast_t* _ast = new ogm_ast_t;
                            _ast->m_type = ogm_ast_t_exp;
                            _ast->m_subtype = ogm_ast_st_exp_literal_primitive;
                            auto* _payload = (ogm_ast_literal_primitive_t*) malloc( sizeof(ogm_ast_literal_primitive_t) );
                            _payload->m_type = ogm_ast_literal_t_number;
                            _payload->m_value = _strdup(std::to_string(i).c_str());;
                            _ast->m_payload = _payload;
                            enum_data.m_map[payload->m_identifier[i]] = _ast;
                        }
                    }
                }
                else
                {
                    throw CompileError(ErrorCode::C::enumrep, ast->m_start, "Error: multiple definitions for enum {}.", enum_name);
                }
            }

            // recurse
            for (size_t i = 0; i < ast->m_sub_count; ++i)
            {
                bytecode_preprocess_helper(&ast->m_sub[i], ast, out_retc, out_argc, in_out_reflection_accumulator, config);
            }
        }
        catch (ogm::Error& error)
        {
            throw error
                .detail<location_start>(ast->m_start)
                .detail<location_end>(ast->m_end);
        }
    }
}

void bytecode_preprocess(DecoratedAST& io_a, ReflectionAccumulator& io_refl, asset::Config* config)
{
    io_a.m_retc = 0;

    // TODO: check if function takes 0 arguments or is variadic by default.
    io_a.m_argc = -1;
    bytecode_preprocess_helper(
        io_a.m_ast, nullptr, io_a.m_retc, io_a.m_argc, io_refl, config
    );
}

}