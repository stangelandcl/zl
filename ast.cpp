#include <assert.h>

#include <functional>

#include "ast.hpp"

#include "parse.hpp"
#include "parse_op.hpp"
#include "parse_decl.hpp"

#include "hash-t.hpp"

// each AST node pushes the result on the top of the stack
//   unless the type is void

namespace AST {

  typedef int target_bool;
  typedef int target_int;

  const NameSpace * DEFAULT_NS = new NameSpace(".default");
  const NameSpace * TAG_NS = new NameSpace(".tag");

  template <typename T>
  void add_ast_nodes(T & container, AST * node);

  void AST::print() {
    printf(" (%s", name_.c_str());
    for (int i = 0; i != parse_->num_args(); ++i) {
      part(i)->print();
    }
    printf(")");
  }

  struct CT_Value_Map {
    String type;
    CT_Value_Base * ct_value;
  };

  template <template <typename> class W> 
  const CT_Value_Base * int_op_ct_value(const Type * t) {
    static CT_Value_Map map[] = {
      {".uint8", new W<uint8_t>},
      {".uint16", new W<uint16_t>},
      {".uint32", new W<uint32_t>},
      {".uint64", new W<uint64_t>},
      {".int8", new W<int8_t>},
      {".int16", new W<int16_t>},
      {".int32", new W<int32_t>},
      {".int64", new W<int64_t>}      
    };
    static CT_Value_Map * map_end = map + sizeof(map)/sizeof(CT_Value_Map);
    String n = t->exact_type->to_string();
    for (const CT_Value_Map * i = map; i != map_end; ++i) {
      if (i->type == n) return i->ct_value;
    }
    return NULL;
  }

  template <template <typename> class W, template <typename> class F>
  struct OCTV_Proxy {
    template <typename T>
    struct Type : public W<F<T> > {};
  };
 
  template <template <typename> class W> 
  const CT_Value_Base * op_ct_value(const Type * t) {
    static CT_Value_Map map[] = {
      {"float", new W<float>},
      {"double", new W<double>},
      {"long double", new W<long double>},
    };
    static CT_Value_Map * map_end = map + sizeof(map)/sizeof(CT_Value_Map);
    String n = t->exact_type->to_string();
    for (const CT_Value_Map * i = map; i != map_end; ++i) {
      if (i->type == n) return i->ct_value;
    }
    return int_op_ct_value<W>(t);
  }

  template <template <typename> class W, template <typename> class F> 
  inline const CT_Value_Base * int_op_ct_value(const Type * t) {
    return int_op_ct_value<OCTV_Proxy<W,F>::template Type>(t);
  }

  template <template <typename> class W, template <typename> class F> 
  inline const CT_Value_Base * op_ct_value(const Type * t) {
    return op_ct_value<OCTV_Proxy<W,F>::template Type>(t);
  }

  AST * parse_top_level(const Parse * p, ParseEnviron & env);
  AST * parse_member(const Parse * p, ParseEnviron & env);
  AST * parse_stmt(const Parse * p, ParseEnviron & env);
  AST * parse_stmt_decl(const Parse * p, ParseEnviron & env);
  AST * parse_exp(const Parse * p, ParseEnviron & env);

  struct NoOp : public AST {
    NoOp() : AST("noop") {}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(0);
      type = env.void_type();
      return this;
    }
    void eval(ExecEnviron &) {}
    void compile(CompileWriter & f, CompileEnviron &) {
      f << indent << "noop();\n";
    }
  };

  struct Empty : public AST {
    Empty() : AST("empty") {}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(0);
      type = env.void_type();
      return this;
    }
    void eval(ExecEnviron &) {}
    void compile(CompileWriter & f, CompileEnviron &) {
      // do absolutely nothing
    }
  };

  struct Terminal : public AST {
    Terminal(const Parse * p) : AST(p->name, p) {}
    AST * parse_self(const Parse * p, ParseEnviron & env) {abort();}
    void eval(ExecEnviron & env) {abort();}
    void compile(CompileWriter & f, CompileEnviron &) {abort();}
  };

  struct Generic : public AST {
    Vector<AST *> parts;
    Generic(const Parse * p) 
      : AST(p->name, p) {
      for (int i = 0; i != p->num_args(); ++i)
        parts.push_back(new Terminal(p->arg(i)));
    }
    Generic(const Parse * p, const Vector<AST *> & pts) 
      : AST(p->name, p), parts(pts) {}
    AST * part(unsigned i) {return parts[i];}
    AST * parse_self(const Parse*, ParseEnviron&) {abort();}
    void eval(ExecEnviron & env) {abort();}
    void compile(CompileWriter & f, CompileEnviron &) {abort();}
  };

  struct LStmt : public AST {
    LStmt() : AST("lstmt") {}
    bool local;
    String label;
    AST * stmt;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      label = p->arg(0)->arg(0)->name;
      if (p->arg(0)->name == "local") {
        env.labels->add(label, label);
        local = true;
      } else {
        env.labels->root->add(label, label);
      }
      stmt = parse_stmt(p->arg(1), env);
      type = stmt->type;
      return this;
    }
    void eval(ExecEnviron & env) {
      stmt->eval(env);
    }
    void compile(CompileWriter & o, CompileEnviron &) {
      o << adj_indent(-2) << indent << o.label_map->lookup(label) << ":\n";
      o << stmt;
    }
  };

  struct LCStmt : public AST {
    LCStmt() : AST("lcstmt") {}
    AST * exp; 
    AST * stmt;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      if (p->arg(0)->name == "case") {
        exp = parse_exp(p->arg(0)->arg(0), env);
      } else /* default */ {
      }
      stmt = parse_stmt(p->arg(1), env);
      type = stmt->type;
      return this;
    }
    void eval(ExecEnviron & env) {
      stmt->eval(env);
    }
    void compile(CompileWriter & o, CompileEnviron &) {
      if (exp)
        o << adj_indent(-2) << indent << "case " << exp << ":\n";
      else
        o << adj_indent(-2) << indent << "default:\n";
      o << stmt;
    }
    
  };

  struct Goto : public AST {
    Goto() : AST("goto") {}
    String label;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      assert(p->arg(0)->name == "id");
      label = p->arg(0)->arg(0)->name;
      return this;
    }
    // FIXME, move into compile ...
    //void resolve(ResolveEnviron & env) {
    //  if (!env.labels->exists(label))
    //    throw error(parse_->arg(0)->arg(0), "Unknown label %s", ~label);
    //  type = env.void_type();
    //}
    void eval(ExecEnviron&) {abort();}
    void compile(CompileWriter & o, CompileEnviron &) {
      o << indent << "goto " << o.label_map->lookup(label) << ";\n";
    }
  };

  AST * Literal::part(unsigned i) {return new Terminal(parse_->arg(0));}
  
  CT_Value_Base * new_literal_ct_value(const Parse * vp, const Type * & t, ParseEnviron & env);

  AST * Literal::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(1,2);
    type = env.types->inst(p->num_args() > 1 ? p->arg(1)->name : "int");
    ct_value_ = new_literal_ct_value(p->arg(0), type, env);
    return this;
  }
  //void Literal::eval(ExecEnviron & env) {
    //env.ret<int>(this) = value;
  //}
  void Literal::compile(CompileWriter & f, CompileEnviron &) {
    ct_value_->to_string(this, f);
    //f.printf("%lld", value);
    String tname = type->unqualified->to_string();
    if (tname == "unsigned int")
      f << "u";
    else if (tname == "long")
      f << "l";
    else if (tname == "unsigned long")
      f << "ul";
    else if (tname == "long long")
      f << "ll";
    else if (tname == "unsigned long long")
      f << "ull";
    else if (tname != "int")
      abort(); // unsupported type;
  }
  
  template <typename T>
  struct Literal_Value : public CT_Value<T> {
    T v;
    Literal_Value(T v0) : v(v0) {}
    T value(const AST *) const {return v;}
  };

  CT_Value_Base * new_literal_ct_value(const Parse * vp, const Type * & t, ParseEnviron & env) {
    const Int * it = dynamic_cast<const Int *>(t->unqualified);
    // FIXME: Need to promote type as indicated in the standard if
    //   the specified type is too small for the literal
    // FIXME: Do I need to make into ct_const type
    if (it->min == 0) {
      const char * s = vp->name.c_str();
      char * e = (char *)s;
      unsigned long long value = strtoull(s, &e, 0);
      if (*e) throw error(vp, "Expected Integer");
      if (value == 0) t = env.types->inst(".zero", t);
      String n = it->exact_type->to_string();
      if (n == ".uint8")
        return new Literal_Value<uint8_t>(value);
      else if (n == ".uint16")
        return new Literal_Value<uint16_t>(value);
      else if (n == ".uint32")
        return new Literal_Value<uint32_t>(value);
      else if (n == ".uint64")
        return new Literal_Value<uint64_t>(value);
      else
        abort();
    } else {
      const char * s = vp->name.c_str();
      char * e = (char *)s;
      long long value = strtoll(s, &e, 0);
      if (*e) throw error(vp, "Expected Integer");
      if (value == 0) t = env.types->inst(".zero", t);
      String n = it->exact_type->to_string();
      if (n == ".int8")
        return new Literal_Value<int8_t>(value);
      else if (n == ".int16")
        return new Literal_Value<int16_t>(value);
      else if (n == ".int32")
        return new Literal_Value<int32_t>(value);
      else if (n == ".int64")
        return new Literal_Value<int64_t>(value);
      else
        abort();
    }
  }

  CT_Value_Base * new_float_ct_value(const Parse * vp, const Type * & t, ParseEnviron & env);

  AST * FloatC::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(1,2);
    type = env.types->inst(p->num_args() > 1 ? p->arg(1)->name : "double");
    ct_value_ = new_float_ct_value(p->arg(0), type, env);
    return this;
  }

  void FloatC::compile(CompileWriter & f, CompileEnviron &) {
    ct_value_->to_string(this, f);
    String tname = type->unqualified->to_string();
    if (tname == "float")
      f << "f";
    else if (tname == "long double")
      f << "l";
    else if (tname != "double")
      abort(); // unsupported type;
  }

  CT_Value_Base * new_float_ct_value(const Parse * vp, const Type * & t, ParseEnviron & env) {
    // FIXME: Do I need to make into ct_const type
    String n = t->exact_type->to_string();
    const char * s = vp->name.c_str();
    char * e = (char *)s;
    long double value = strtold(s, &e);
    if (*e) throw error(vp, "Expected Number");
    if (n == "float")
      return new Literal_Value<float>(value);
    else if (n == "double")
      return new Literal_Value<double>(value);
    else if (n == "long double")
      return new Literal_Value<long double>(value);
    else
      abort();
  }

  AST * StringC::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(1,2);
    orig = p->arg(0)->name;
    type = env.types->inst(".pointer", env.types->ct_const(env.types->inst("char")));
    type = env.types->ct_const(type);
    return this;
  }
  void StringC::compile(CompileWriter & f, CompileEnviron &) {
    f << orig;
  }

  AST * CharC::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(1, 2);
    orig = p->arg(0)->name;
    type = env.types->inst("char");
    type = env.types->ct_const(type);
    return this;
  }
  void CharC::compile(CompileWriter & f, CompileEnviron &) {
    f << orig;
  }

  struct Id : public AST {
    Id() : AST("id") {}
    AST * part(unsigned i) {return new Terminal(parse_->arg(0));}
    String name;
    Symbol * sym;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      name = p->arg(0)->name;
      if (!env.vars->exists(name))
        throw error(parse_->arg(0), "Unknown Identifier: %s", name.c_str());
      sym = env.vars->lookup(name);
      if (sym->ct_value)
        ct_value_ = sym->ct_value;
      type = sym->type;
      lvalue = true;
      return this;
    }
    // these might needed to moved into eval_prep
    //void resolve(ResolveEnviron & env) {
    //  return_offset = env.frame->alloc_tmp(type);
    //}
    //void resolve_lvalue(ResolveEnviron & env) {
    //  if (!env.vars->exists(name))
    //    throw error(parse_->arg(0), "Unknown Identifier: %s", name.c_str());
    //  sym = env.vars->lookup(name);
    //  type = sym->type;
    //  addr = *sym;
    //}
    void eval(ExecEnviron & env) {
      //if (sym->value) { // compile time constant
      //  copy_val(env.ret(this), sym->value, type);
      //} else {
      //  copy_val(env.ret(this), env.var(*sym), type);
      // }
    }
    void compile(CompileWriter & f, CompileEnviron &) {
      f << name;
    }
  };

  struct If : public AST {
    If() : AST("if") {}
    AST * part(unsigned i) 
      {return i == 0 ? exp : i == 1 ? if_true : i == 2 ? if_false : 0;}
    AST * exp;
    AST * if_true;
    AST * if_false;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2,3);
      exp = parse_exp(p->arg(0), env);
      if_true = parse_stmt(p->arg(1), env);
      if (p->num_args() == 3) {
        if_false = parse_stmt(p->arg(2), env);
      } else {
        if_false = new NoOp;
      }
      type = env.void_type();
      resolve_to(env, exp, env.bool_type());
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  env.frame->pop_tmp(exp->type);
    // resolve_to_void(env, if_true);
    //  resolve_to_void(env, if_false);
    //}
    void eval(ExecEnviron & env) {
      exp->eval(env);
      bool res = env.ret<int>(this);
      if (res) if_true->eval(env);
      else     if_false->eval(env);
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "if (" << exp << ")\n";
      f << adj_indent(2) << if_true;
      f << indent << "else\n";
      f << adj_indent(2) << if_false;
    }
  };

  struct EIf : public AST {
    EIf() : AST("eif") {}
    AST * part(unsigned i) 
      {return i == 0 ? exp : i == 1 ? if_true : i == 2 ? if_false : 0;}
    AST * exp;
    AST * if_true;
    AST * if_false;
    AST * parse_self(const Parse * p, ParseEnviron & env);
    //void resolve(ResolveEnviron & env) {
    //  resolve_to(env, exp, env.bool_type());
    //  env.frame->pop_tmp(exp->type);
    //  if_true->resolve(env);
    //  env.frame->pop_tmp(if_true->type);
    //  resolve_to(env, if_false, if_true->type);
    //  type = if_true->type;
    //}
    void eval(ExecEnviron & env) {
      exp->eval(env);
      bool res = env.ret<int>(this);
      if (res) if_true->eval(env);
      else     if_false->eval(env);
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "(" << exp << " ? " << if_true << " : " << if_false << ")";
    }
  };

  template <typename T> 
  struct EIf_CT_Value : public CT_Value<T> {
    T value(const AST * a) const {
      const EIf * eif = dynamic_cast<const EIf *>(a);
      if (eif->exp->ct_value<target_bool>())
        return eif->if_true->ct_value<T>();
      else
        return eif->if_false->ct_value<T>();
    }
  };

  AST * EIf::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(3);
    exp = parse_exp(p->arg(0), env);
    resolve_to(env, exp, env.bool_type());
    if_true = parse_exp(p->arg(1), env);
    if_false = parse_exp(p->arg(2), env);
    resolve_to(env, if_false, if_true->type);
    type = if_true->type;
    ct_value_ = op_ct_value<EIf_CT_Value>(type);
    return this;
  }

  struct Switch : public AST {
    Switch() : AST("switch") {}
    AST * exp;
    AST * body;
    AST * part(unsigned i) {return i == 0 ? exp : i == 1 ? body : 0;}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      exp = parse_exp(p->arg(0), env);
      resolve_to(env, exp, env.bool_type());  
      body = parse_stmt(p->arg(1), env);
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  type = env.void_type();
    //  resolve_to(env, exp, env.bool_type());      
    //  resolve_to_void(env, body);
    //}
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "switch (" << exp << ")\n";
      f << adj_indent(2) << body;
    }
  };


  struct Loop : public AST {
    Loop() : AST("loop") {}
    AST * part(unsigned i) 
      {return body;}
    AST * body;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      body = parse_stmt(p->arg(0), env);
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  resolve_to_void(env, body);
    //}
    void eval(ExecEnviron & env) {
      try {
        for (;;)
          body->eval(env);
      } catch (BreakException) {
      }
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "for (;;)\n";
      f << body;
    }
  };
  
  struct Break : public AST {
    Break() : AST("break") {}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(0);
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //}
    void eval(ExecEnviron &) {
      throw BreakException();
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "break;\n";
    }
  };

  struct Var : public Declaration {
    Var() : Declaration("var"), init() {}
    AST * part(unsigned i) {return new Terminal(parse_->arg(0));}
    String name;
    Type * var_type;
    AST * init;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2,3);
      name = p->arg(0)->name;
      parse_flags(p);
      Symbol * s = new Symbol(name);
      s->type = var_type = parse_type(env.types, p->arg(1), env);
      if (storage_class != EXTERN && var_type->size() == NPOS) 
        throw error(p->arg(0), "Size not known");
      env.vars->add(name, s);
      if (p->num_args() > 2) {
        init = parse_exp(p->arg(2), env);
        resolve_to(env, init, var_type);
      }
      return this;
      type = env.void_type();
    }
    void eval(ExecEnviron &) {}
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent;
      write_flags(f);
      StringBuf buf;
      c_print_inst->declaration(name, *var_type, buf);
      f << buf.freeze();
      if (init)
        f << " = " << init;
      f << ";\n";
    }
  };

  struct EStmt : public AST {
    EStmt() : AST("estmt") {}
    EStmt(AST * e) : AST("estmt"), exp(e) {type = exp->type;}
    AST * part(unsigned i) {return exp;}
    AST * exp;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      exp = parse_exp(p->arg(0), env);
      type = exp->type;
      return this;
    }
    void eval(ExecEnviron & env) {
      exp->eval(env);
    };
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << exp << ";\n";
    }
  };

  struct BlockBase : public AST {
    BlockBase(String name, bool ae) : AST(name), as_exp(ae) {}
    bool as_exp;
    hash_map<String, unsigned> local_lables;
    LabelSymbolTable * labels;
    TypeSymbolTable * types;
    VarSymbolTable * vars;
    AST * part(unsigned i) {return stmts[i];}
    Vector<AST *> stmts;
    AST * parse_self(const Parse * p, ParseEnviron & env0) {
      parse_ = p;
      ParseEnviron env = env0.new_scope();
      labels = env.labels = new LabelSymbolTable(env0.labels);
      types = env.types;
      vars  = env.vars;
      if (p->num_args() > 0) {
        for (int i = 0; i < p->num_args(); ++i) {
          add_ast_nodes(stmts, parse_stmt_decl(p->arg(i), env));
        }
      } else {
        stmts.push_back(new NoOp);
      }
      if (as_exp) 
        type = stmts.back()->type;
      else
        type = env.void_type();
      return this;
    }
    void eval(ExecEnviron & env) {
      for (int i = 0; i != stmts.size(); ++i) {
        stmts[i]->eval(env);
      }
    };
    void compile(CompileWriter & f, CompileEnviron & env) {
      for (LabelSymbolTable::Symbols::iterator 
             i = labels->symbols.begin(), e = labels->symbols.end();
           i != e; ++i)
      {
        String n;
        int j = 0;
        StringBuf new_name;
        do {
          new_name.printf("%s__%d", ~i->first.second+1 /*FIXME: Hack*/, j++);
          n = new_name.freeze();
        } while (f.label_names.have(n));
        f.label_names.insert(n);
        i->second = n;
      }
      f.label_map = labels;
      if (as_exp)
        f << "({\n";
      else
        f << indent << "{\n";
      for (int i = 0; i != stmts.size(); ++i) {
        f << adj_indent(2) << stmts[i];
      }
      if (as_exp)
        f << indent << "})";
      else
        f << indent << "}\n";
      f.label_map = f.label_map->parent;
    }
  };

  struct Block : public BlockBase {
    Block() : BlockBase("block", false) {}
  };

  struct EBlock : public BlockBase {
    EBlock() : BlockBase("eblock", true) {}
  };

  struct Print : public AST {
    Print() : AST("print") {}
    AST * part(unsigned i) {return exp;}
    AST * exp;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      exp = parse_exp(p->arg(0), env);
      resolve_to(env, exp, env.types->inst("int"));
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  env.frame->pop_tmp(exp->type);
    //}
    void eval(ExecEnviron & env) {
      exp->eval(env);
      printf("%d\n", env.ret<int>(exp));
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "printf(\"%d\\n\", " << exp << ");\n";
    }
  };

  void check_type(AST * exp, TypeCategory * cat) {
    if (!exp->type->is(cat)) 
      throw error(exp->parse_, "Expected %s type", ~cat->name);
  }

  struct UnOp : public AST {
    UnOp(String name, String op0) : AST(name), op(op0) {}
    AST * part(unsigned i) {return exp;}
    AST * exp;
    String op;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      exp = parse_exp(p->arg(0), env);
      resolve(env);
      make_ct_value(env);
      return this;
    }
    virtual void resolve(ParseEnviron & env) = 0;
    virtual void make_ct_value(ParseEnviron & env) {}
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "(" << op << " " << exp << ")";
    }
  };

  const Type * resolve_unop(ResolveEnviron & env, TypeCategory * cat, AST * exp) {
    if (!exp->type->is(cat))
      abort();
    return exp->type;
  }

  template <typename F> 
  struct UnOp_CT_Value : public CT_Value<typename F::result_type> {
    typename F::result_type value(const AST * a) const {
      const UnOp * b = dynamic_cast<const UnOp *>(a);
      typename F::argument_type x = b->exp->ct_value<typename F::argument_type>();
      return F()(x);
    }
  };

  struct SimpleUnOp : public UnOp {
    SimpleUnOp(String name, String op0, TypeCategory * c) 
      : UnOp(name, op0), category(c) {}
    TypeCategory * category;
    void resolve(ResolveEnviron & env) {
      type = resolve_unop(env, category, exp);
    }
  };

  struct UPlus : public SimpleUnOp {
    UPlus() : SimpleUnOp("uplus", "+", NUMERIC_C) {}
    void make_ct_value(ResolveEnviron &) {
      ct_value_ = exp->ct_value_;
    }
  };

  struct Neg : public SimpleUnOp {
    Neg() : SimpleUnOp("neg", "-", NUMERIC_C) {}
    void make_ct_value(ResolveEnviron &) {
      ct_value_ = op_ct_value<UnOp_CT_Value, std::negate>(type);
    }
  };

  struct Compliment : public SimpleUnOp {
    Compliment() : SimpleUnOp("compliment", "~", INT_C) {}
    template <typename T>
    struct F : public std::unary_function<T,T> {
      T operator()(T x) {return ~x;}
    };
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = int_op_ct_value<UnOp_CT_Value, F>(type);
    }
  };

  struct Not : public UnOp {
    Not() : UnOp("not", "!") {}
    void resolve(ResolveEnviron & env) {
      abort();
    }
  };

  struct AddrOf : public UnOp {
    AddrOf() : UnOp("addrof", "&") {}
    void resolve(ResolveEnviron & env) {
      if (!exp->lvalue) {
        throw error(exp->parse_, "Can not be used as lvalue");
      }
      // FIXME: add check for register qualifier
      const TypeSymbol * t = env.types->lookup(".pointer");
      Vector<TypeParm> p;
      p.push_back(TypeParm(exp->type));
      type = t->inst(p);
    }
  };

  struct DeRef : public UnOp {
    DeRef() : UnOp("deref", "*") {}
    void resolve(ResolveEnviron & env) {
      check_type(exp, POINTER_C);
      const PointerLike * t = dynamic_cast<const PointerLike *>(exp->type->unqualified);
      type = t->subtype;
      lvalue = true;
    }
  };

  struct BinOp : public AST {
    BinOp(String name, String op0) : AST(name), op(op0) {}
    AST * part(unsigned i) {return i == 0 ? lhs : rhs;}
    AST * lhs;
    AST * rhs;
    String op;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      lhs = parse_exp(p->arg(0), env);
      rhs = parse_exp(p->arg(1), env);
      resolve(env);
      make_ct_value(env);
      return this;
    }
    virtual void resolve(ParseEnviron & env) = 0;
    virtual void make_ct_value(ParseEnviron & env) {}
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "(" << lhs << " " << op << " " << rhs << ")";
    }
  };

  struct MemberAccess : public AST {
    MemberAccess() : AST("member") {}
    AST * part(unsigned i) {abort();}
    AST * exp;
    String id;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      exp = parse_exp(p->arg(0), env);
      if (p->arg(1)->name != "id") throw error(p->arg(1), "Expected identifier");
      id = p->arg(1)->arg(0)->name;
      const StructUnionT * t = dynamic_cast<const StructUnionT *>(exp->type->unqualified);
      if (!t) throw error(p->arg(0), "Expected struct or union type");
      if (!t->defined) throw error(p->arg(1), "Invalid use of incomplete type");
      if (!t->env->vars->exists(id))
        throw error(p->arg(1), "\"%s\" is not a member of \"%s\"", 
                    ~id, ~t->to_string());
      type = t->env->vars->lookup(id)->type;
      lvalue = true;
      return this;
    };
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "((" << exp << ")" << "." << id << ")";
    }
  };

  const Type * remove_qualifiers(const Type * t) {
    return t->unqualified;
  }

  const Type * resolve_binop(ResolveEnviron & env, TypeCategory * cat, AST *& lhs, AST *& rhs) {
    check_type(lhs, cat);
    check_type(rhs, cat);
    const Type * t = env.type_relation->unify(0, lhs, rhs);
    return t;
  }

  template <typename F> 
  struct BinOp_CT_Value : public CT_Value<typename F::result_type> {
    typename F::result_type value(const AST * a) const {
      const BinOp * b = dynamic_cast<const BinOp *>(a);
      typename F::first_argument_type x = b->lhs->ct_value<typename F::first_argument_type>();
      typename F::second_argument_type y = b->rhs->ct_value<typename F::second_argument_type>();
      return F()(x, y);
    }
  };

  template <typename F> 
  struct Comp_CT_Value : public CT_Value<target_bool> {
    int value(const AST * a) const {
      const BinOp * b = dynamic_cast<const BinOp *>(a);
      typename F::first_argument_type x = b->lhs->ct_value<typename F::first_argument_type>();
      typename F::second_argument_type y = b->rhs->ct_value<typename F::second_argument_type>();
      return F()(x, y);
    }
  };
  
  const Type * p_subtype(const Type * t) {
    if (const Pointer * p = dynamic_cast<const Pointer *>(t))
      return p->subtype;
    if (const Array   * p = dynamic_cast<const Array *>(t))
      return p->subtype;
    return VOID_T;
    //abort();
  }

  enum PointerBinOp {P_MINUS, P_COMP};
  void resolve_pointer_binop(PointerBinOp op, ResolveEnviron & env, AST *& lhs, AST *& rhs) {
    check_type(lhs, POINTER_C);
    check_type(rhs, POINTER_C);
    const Type * lhs_subtype = p_subtype(lhs->type->unqualified)->unqualified;
    const Type * rhs_subtype = p_subtype(rhs->type->unqualified)->unqualified;
    if (op == P_MINUS) {
      if (lhs_subtype == rhs_subtype) return;
    } else if (op == P_COMP) {
      if (lhs_subtype == rhs_subtype
          || lhs->type->is_null || lhs->type->is_null 
          || dynamic_cast<const Void *>(lhs_subtype) 
          || dynamic_cast<const Void *>(rhs_subtype))
        return;
    } else {
      abort();
    }
    throw error(rhs->parse_, "Incompatible pointer types");
  }
  
  const Type * resolve_additive(ResolveEnviron & env, AST *& lhs, AST *& rhs) {
    check_type(lhs, SCALAR_C);
    check_type(rhs, SCALAR_C);
    if (lhs->type->is(NUMERIC_C) && rhs->type->is(NUMERIC_C)) {
      return resolve_binop(env, NUMERIC_C, lhs, rhs);
    } else if (lhs->type->is(POINTER_C)) {
      check_type(rhs, INT_C);
      return lhs->type;
    } else if (lhs->type->is(INT_C)) {
      check_type(rhs, POINTER_C);
      return rhs->type;
    } else {
      abort(); // this should't happen
    }
  }

  struct Assign : public BinOp {
    Assign() : BinOp("assign", "=") {}
    void resolve(ResolveEnviron & env) {
      if (!lhs->lvalue)
        throw error(lhs->parse_, "Can not be used as lvalue");
      if (lhs->type->read_only) 
        throw error (lhs->parse_, "Assignment to read-only location");
      resolve_to(env, rhs, lhs->type);
      type = lhs->type;
    }
  };

  struct CompoundAssign : public BinOp {
    CompoundAssign(String name, String op0, BinOp *bop, ParseEnviron & env) 
      : BinOp(name, op0), assign(new Assign), binop(bop) 
    {
      parse_ = binop->parse_;
      lhs = binop->lhs;
      rhs = binop->rhs;
      assign->parse_ = parse_;
      assign->lhs = lhs;
      assign->rhs = binop;
      assign->resolve(env);
      type = assign->type;
    }
    Assign * assign;
    BinOp * binop;
    AST * parse_self(const Parse * p, ParseEnviron & env) {abort();}
    void resolve(ResolveEnviron & env) {abort();}
  };

  struct SimpleBinOp : public BinOp {
    SimpleBinOp(String name, String op0, TypeCategory * c) 
      : BinOp(name, op0), category(c) {}
    TypeCategory * category;
    void resolve(ResolveEnviron & env) {
      type = resolve_binop(env, category, lhs, rhs);
    }
  };

  struct Plus : public BinOp {
    Plus() : BinOp("plus", "+") {}
    void resolve(ResolveEnviron & env) {
      type = resolve_additive(env, lhs, rhs);
    }
    void make_ct_value(ParseEnviron & env) {
      if (lhs->type->is(NUMERIC_C) && rhs->type->is(NUMERIC_C))
        ct_value_ = op_ct_value<BinOp_CT_Value, std::plus>(type);
    }
  };

  struct Minus : public BinOp {
    Minus() : BinOp("minus", "-") {}
    void resolve(ResolveEnviron & env) {
      try {
        type = resolve_additive(env, lhs, rhs);
      } catch(...) {
        if (lhs->type->is(POINTER_C) || rhs->type->is(POINTER_C)) {
          resolve_pointer_binop(P_MINUS, env, lhs, rhs);
          type = env.types->inst("int");
        }
        else
          throw;
      }
    }
    void make_ct_value(ParseEnviron & env) {
      if (lhs->type->is(NUMERIC_C) && rhs->type->is(NUMERIC_C))
        ct_value_ = op_ct_value<BinOp_CT_Value, std::minus>(type);
    }
  };

  struct Times : public SimpleBinOp {
    Times() : SimpleBinOp("times", "*", NUMERIC_C) {}
    void make_ct_value() {
      ct_value_ = op_ct_value<BinOp_CT_Value, std::multiplies>(type);
    }
  };

  struct Div : public SimpleBinOp {
    Div() : SimpleBinOp("div", "/", NUMERIC_C) {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<BinOp_CT_Value, std::divides>(type);
    }
  };

  struct Mod : public SimpleBinOp {
    Mod() : SimpleBinOp("mid", "%", INT_C) {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = int_op_ct_value<BinOp_CT_Value, std::modulus>(type);
    }
  };

  struct BAnd : public SimpleBinOp { 
    BAnd() : SimpleBinOp("band", "&", INT_C) {}
    template <typename T>
    struct F : public std::binary_function<T,T,T> {
      T operator()(T x, T y) {return x & y;}
    };
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = int_op_ct_value<BinOp_CT_Value, F>(type);
    }
  };

  struct BOr : public SimpleBinOp {
    BOr() : SimpleBinOp("bor", "|", INT_C) {}
    template <typename T>
    struct F : public std::binary_function<T,T,T> {
      T operator()(T x, T y) {return x | y;}
    };
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = int_op_ct_value<BinOp_CT_Value, F>(type);
    }
  };

  struct XOr : public SimpleBinOp {
    XOr() : SimpleBinOp("xor", "^", INT_C) {}
    template <typename T>
    struct F : public std::binary_function<T,T,T> {
      T operator()(T x, T y) {return x ^ y;}
    };
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = int_op_ct_value<BinOp_CT_Value, F>(type);
    }
  };

  struct BShift {
    // NOTE: Resolve sementans are slightly diffrent
  };

  struct CompOp : public BinOp {
    CompOp(String n, String op) : BinOp(n, op) {}
    void resolve(ResolveEnviron & env) {
      check_type(lhs, SCALAR_C);
      check_type(rhs, SCALAR_C);
      if (lhs->type->is(NUMERIC_C)) {
        resolve_binop(env, NUMERIC_C, lhs, rhs);
      } else if (lhs->type->is(POINTER_C)) {
        resolve_pointer_binop(P_COMP, env, lhs, rhs);
      } else {
        abort(); // This should't happen
      }
      type = env.types->inst("<bool>");
    }
  };

  struct Eq : public CompOp {
    Eq() : CompOp("eq", "==") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::equal_to>(type);
    }
  };

  struct Ne : public CompOp {
    Ne() : CompOp("ne", "!=") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::not_equal_to>(type);
    }
  };

  struct Lt : public CompOp {
    Lt() : CompOp("lt", "<") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::less>(type);
    }
  };

  struct Gt : public CompOp {
    Gt() : CompOp("gt", ">") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::greater>(type);
    }
  };

  struct Le : public CompOp {
    Le() : CompOp("le", "<=") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::less_equal>(type);
    }
  };

  struct Ge : public CompOp {
    Ge() : CompOp("ge", ">=") {}
    void make_ct_value(ParseEnviron & env) {
      ct_value_ = op_ct_value<Comp_CT_Value, std::greater_equal>(type);
    }
  };

  struct PostIncDec : public AST {
    PostIncDec(String name, String op0) : AST(name), op(op0) {}
    AST * part(unsigned i) {return exp;}
    AST * exp;
    String op;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      exp = parse_exp(p->arg(0), env);
      type = exp->type;
      return this;
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "(" << exp << " " << op << ")";
    }
  };

  struct PostInc : public PostIncDec {
    PostInc() : PostIncDec("postinc", "++") {}
  };

  struct PostDec : public PostIncDec {
    PostDec() : PostIncDec("postdec", "--") {}
  };

  struct ASTList : public AST {
    ASTList() : AST("astlist") {}
    AST * part(unsigned i) {return stmts[i];}
    Vector<AST *> stmts;
    AST * parse_self(const Parse * p, ParseEnviron & env) {abort();}
    void resolve(ResolveEnviron & env) {abort();} 
    void compile(CompileWriter & f, CompileEnviron & env) {abort();}
  };

  template <typename T>
  void add_ast_nodes(T & container, AST * node) {
    if (!node) {
      // noop
    } else if (ASTList * list = dynamic_cast<ASTList *>(node)) {
      container.insert(container.end(), list->stmts.begin(), list->stmts.end());
    } else {
      container.push_back(node);
    }
  }

  struct SList : public ASTList {
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      //parse_ = p;
      if (p->num_args() > 0) {
        for (int i = 0; i < p->num_args(); ++i) {
          add_ast_nodes(stmts, parse_stmt_decl(p->arg(i), env));
        }
      }
      return this;
    }
  };

  struct Top : public AST {
    Top() : AST("top") {}
    AST * part(unsigned i) {return stmts[i];}
    Vector<AST *> stmts;
    unsigned frame_size;
    TypeSymbolTable * types;
    VarSymbolTable * vars;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      if (p->num_args() > 0) {
        for (int i = 0; i < p->num_args(); ++i) {
          add_ast_nodes(stmts, parse_top_level(p->arg(i), env));
        }
      } else {
        stmts.push_back(new NoOp());
      }
      types = env.types;
      vars = env.vars;
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  printf("***TOP RESOLVE %p***\n", this);
    //  types = env.types;
    //  vars = env.vars;
    //  type = env.void_type();
    //  printf(">>%d\n", stmts.size());
    //  for (int i = 0; i != stmts.size(); ++i) {
    //    resolve_to_void(env, stmts[i]);
    //  }
    //  frame_size = env.frame->max_frame_size;
    //}
    void eval(ExecEnviron & env) {
      for (int i = 0; i != stmts.size(); ++i) {
        stmts[i]->eval(env);
      }
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << "static inline void noop() {}\n";
      f << "\n";
      for (int i = 0; i != stmts.size(); ++i) {
        stmts[i]->compile(f, env);
      }
    }
  };

  template <typename From, typename To>
  struct Cast_CT_Value : public CT_Value<To> {
    To value(const AST * t) const {
      const Cast * c = dynamic_cast<const Cast *>(t);
      return static_cast<To>(c->exp->ct_value<From>());
    }
  };

  struct Cast_CT_Value_Inner_Map {
    String to;
    const CT_Value_Base * cast;
  };

  template <typename From>
  struct Cast_CT_Value_Group {
    static Cast_CT_Value_Inner_Map map[];
    static Cast_CT_Value_Inner_Map * map_end;
  };

  template <typename From>
  Cast_CT_Value_Inner_Map Cast_CT_Value_Group<From>::map[] = {
    {".uint8", new Cast_CT_Value<From, uint8_t>},
    {".uint16", new Cast_CT_Value<From, uint16_t>},
    {".uint32", new Cast_CT_Value<From, uint32_t>},
    {".uint64", new Cast_CT_Value<From, uint64_t>},
    {".int8", new Cast_CT_Value<From, int8_t>},
    {".int16", new Cast_CT_Value<From, int16_t>},
    {".int32", new Cast_CT_Value<From, int32_t>},
    {".int64", new Cast_CT_Value<From, int64_t>},
    {"float", new Cast_CT_Value<From, float>},
    {"double", new Cast_CT_Value<From, double>},
    {"long double", new Cast_CT_Value<From, long double>},
  };
  template <typename From>
  Cast_CT_Value_Inner_Map * Cast_CT_Value_Group<From>::map_end = 
    map + sizeof(Cast_CT_Value_Group<From>::map)/sizeof(Cast_CT_Value_Inner_Map);

  struct Cast_CT_Value_Map {
    String from;
    Cast_CT_Value_Inner_Map * map;
    Cast_CT_Value_Inner_Map * map_end;
    Cast_CT_Value_Map(String f, Cast_CT_Value_Inner_Map * m, Cast_CT_Value_Inner_Map * e)
      : from(f), map(m), map_end(e) {}
  };

  template <typename T>
  static inline Cast_CT_Value_Map make_cast_ct_value_map(String n) {
    return Cast_CT_Value_Map(n, Cast_CT_Value_Group<T>::map, Cast_CT_Value_Group<T>::map_end);
  }

  Cast_CT_Value_Map cast_ct_value_map[] = {
    make_cast_ct_value_map<uint8_t>(".uint8"), 
    make_cast_ct_value_map<uint16_t>(".uint16"), 
    make_cast_ct_value_map<uint32_t>(".uint32"), 
    make_cast_ct_value_map<uint64_t>(".uint64"), 
    make_cast_ct_value_map<int8_t>(".int8"), 
    make_cast_ct_value_map<int16_t>(".int16"), 
    make_cast_ct_value_map<int32_t>(".int32"), 
    make_cast_ct_value_map<int64_t>(".int64"), 
    make_cast_ct_value_map<float>("float"),
    make_cast_ct_value_map<double>("double"),
    make_cast_ct_value_map<long double>("long double"),
  };
  Cast_CT_Value_Map * cast_ct_value_map_end = 
    cast_ct_value_map + sizeof(cast_ct_value_map)/sizeof(Cast_CT_Value_Map);

  const CT_Value_Base * cast_ct_value(const Type * f, const Type * t) {
    String from = f->exact_type->to_string();
    String to   = t->exact_type->to_string();
    for (const Cast_CT_Value_Map * i = cast_ct_value_map; i != cast_ct_value_map_end; ++i) {
      if (i->from == from) {
        for (const Cast_CT_Value_Inner_Map * j = i->map; j != i->map_end; ++j) {
          if (j->to == to)
            return j->cast;
        }
        return NULL;
      }
    }
    return NULL;
  }

  void Cast::compile(CompileWriter & f, CompileEnviron & env) {
    f << "((" << type->to_string() << ")" << "(" << exp << "))";
  };

  AST * ExplicitCast::parse_self(const Parse * p, ParseEnviron & env) {
    parse_ = p;
    assert_num_args(2);
    type = parse_type(env.types, p->arg(0), env);
    exp = parse_exp(p->arg(1), env);
    return this;
  }
  
  AST * Fun::part(unsigned i) {
    if (i == 0) {
      return new Terminal(parse_->arg(0));
    } else if (i == 1) {
      return new Generic(parse_->arg(1));
    } else {
      return body;
    }
  }

  AST * Fun::parse_self(const Parse * p, ParseEnviron & env0) {
    parse_ = p;
    assert_num_args(3,4);
    name = p->arg(0)->name;

    parse_flags(p);

    sym = new Symbol(name);
    env0.vars->root->add(name, sym);

    ParseEnviron env = env0.new_frame();
    labels = env.labels = new LabelSymbolTable();

    parms = dynamic_cast<const Tuple *>(parse_type(env.types, p->arg(1), env));
    assert(parms); // FIXME: Error message
    for (Tuple::Parms::const_iterator i = parms->parms.begin(), e = parms->parms.end();
         i != e; ++i)
    {
      String name = i->name;
      Symbol * sym = new Symbol(name);
      sym->type = i->type;
      env.vars->add(name, sym);
    }

    ret_type = env.frame->return_type = parse_type(env.types, p->arg(2), env);

    body = 0;
    if (p->num_args() > 3) {
      body = dynamic_cast<Block *>(parse_stmt(p->arg(3), env));
      assert(body); // FiXME
    }

    type = ret_type;

    sym->type = env.function_sym()->inst(env.types, this);
    //sym->value = this;

    return this;
  }

//   void Fun::resolve(ResolveEnviron & env0) {
//     printf("RESOLVE FUN %s\n", name.c_str());
//     sym = new Symbol(name);
//     env0.vars->root->add(name, sym);
    
//     return_offset = 0;
    
//     ResolveEnviron env = env0.new_frame();
//     env.labels = labels;
//     parms = dynamic_cast<const Tuple *>(parse_type(env.types, parms_parse));
//     assert(parms); // FIXME: Move check up to parse
//     ret_type = parse_type(env.types, ret_type_parse);
//     env.frame->return_type = ret_type;
//     env.frame->alloc_var(ret_type);
    
// //     int num_parms = parms.size();
// //     for (int i = 0; i != num_parms; ++i) {
// //       String name = parms[i].name;
// //       parms[i].type = parse_type(env.types, parms[i].type_parse);
// //       Symbol * sym = new Symbol(name);
// //       sym->type = parms[i].type;
// //       env.vars->add(name, sym);
// //       env.frame->alloc_var(sym->type);
// //     }
//     frame_offset = env.frame->max_frame_size;
//     if (body)
//       resolve_to_void(env, body);
//     frame_size = env.frame->max_frame_size;

//     type = ret_type;

//     sym->type = env.function_sym()->inst(env.types, this);
//     sym->value = this;
//   }

  void Fun::eval(ExecEnviron & env) {
      // caller already set up a new frame, the stack pointer
      // points to the return_value
    env.alloc(frame_size);
    try {
      body->eval(env);
    } catch (ReturnException) {
    }
  }

  void Fun::compile(CompileWriter & f, CompileEnviron & env) {
    f.label_names.clear();
    write_flags(f);
    StringBuf buf;
    c_print_inst->declaration(name, *sym->type, buf);
    f << buf.freeze();
    if (body)
      f << "\n" << body;
    else
      f << ";\n";
    f.label_map = 0;
  }

  struct Return : public AST {
    AST * what;
    Return() : AST("return") {}
    AST * part(unsigned i) {return what;}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      what = parse_exp(p->arg(0), env);
      resolve_to(env, what, env.frame->return_type);
      type = env.void_type();
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  resolve_to(env, what, env.frame->return_type);
    //  env.frame->pop_tmp(what->type);
    //  type = env.void_type();
    //}
    void eval(ExecEnviron & env) {
      what->eval(env);
      copy_val(env.local_var(0), env.ret(what), what->type);
      throw ReturnException();
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << indent << "return " << what << ";\n";
    }
  };

  struct Call : public AST {
    Call() : AST("call") {} 
    AST * part(unsigned i) {return i == 0 ? lhs : new Generic(parse_->arg(1), parms);}
    AST * lhs;
    Vector<AST *> parms;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      lhs = parse_exp(p->arg(0), env);
      p = p->arg(1);
      const int num_parms = p->num_args();
      for (int i = 0; i != num_parms; ++i) {
        parms.push_back(parse_exp(p->arg(i), env));
      }
      const FunctionPtr * ftype = dynamic_cast<const FunctionPtr *>(lhs->type);
      if (!ftype) {
        if (const Pointer * t = dynamic_cast<const Pointer *>(lhs->type))
          ftype = dynamic_cast<const FunctionPtr *>(t->subtype);
      }
      if (!ftype)
        throw error (lhs->parse_, "Expected function type");
      type = ftype->ret;
      if (!ftype->parms->vararg && parms.size() != ftype->parms->parms.size()) 
        throw error(parse_->arg(1), 
                    "Wrong number of parameters, expected %u but got %u",
                    ftype->parms->parms.size(), parms.size());
      else if (ftype->parms->vararg && parms.size() < ftype->parms->parms.size())
	throw error(parse_->arg(1),
		    "Not enough parameters, expected at least %u but got %u",
		    ftype->parms->parms.size(), parms.size());
      const int typed_parms = ftype->parms->parms.size();
      for (int i = 0; i != typed_parms; ++i) {
        resolve_to(env, parms[i], ftype->parms->parms[i].type);
      }
      return this;
    }
    //void resolve(ResolveEnviron & env) {
    //  lhs->resolve(env);
    //  env.frame->pop_tmp(lhs->type);
    //  const FunctionPtr * ftype = dynamic_cast<const FunctionPtr *>(lhs->type);
    //  if (!ftype) {
    //    if (const Pointer * t = dynamic_cast<const Pointer *>(lhs->type))
    //      ftype = dynamic_cast<const FunctionPtr *>(t->subtype);
    //  }
    //  if (!ftype)
    //    throw error (lhs->parse_, "Expected function type");
    //  type = ftype->ret;
    //  printf(">>RET>%s\n", ~ftype->ret->to_string());
    //  return_offset = env.frame->alloc_tmp(ftype);
    //  unsigned saved_size = env.frame->cur_frame_size;
    //  if (parms.size() != ftype->parms->parms.size()) 
    //    throw error(parse_->arg(1), 
    //                "Wrong number of parameters, expected %u but got %u",
    //                ftype->parms->parms.size(), parms.size());
    //  const int num_parms = parms.size();
    //  for (int i = 0; i != num_parms; ++i) {
    //    resolve_to(env, parms[i], ftype->parms->parms[i].type);
    //  }
    //  env.frame->pop_to(saved_size);
    //}
    void eval(ExecEnviron & env) {
      lhs->eval(env);
      Fun * f = env.ret<Fun *>(lhs);
      const int num_parms = parms.size();
      for (int i = 0; i != num_parms; ++i)
        parms[i]->eval(env);
      ExecEnviron env2 = env.new_frame(return_offset);
      f->eval(env2);
    }
    void compile(CompileWriter & f, CompileEnviron & env) {
      f << lhs << "(";
      int i = 0;
      if (i != parms.size()) while (true) {
        parms[i]->compile(f, env);
        ++i;
        if (i == parms.size()) break;
        f.printf(", ");
      }
      f << ")";
    }
  };

  struct TypeAlias : public AST {
    TypeAlias() : AST("talias") {}
    String name;
    const Type * type;
    AST * part(unsigned i) {abort();}
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(2);
      name = p->arg(0)->name;
      type = parse_type(env.types, p->arg(1), env);
      add_simple_type(env.types, name, new AliasT(type));
      return this;
    }
    void compile(CompileWriter & f, CompileEnviron &) {
      f << indent << "typedef ";
      StringBuf buf;
      c_print_inst->declaration(name, *type, buf);
      f << buf.freeze();
      f << ";\n";
    }
  };

  struct StructUnion : public AST {
    enum What {STRUCT, UNION} what;
    struct Body : public AST {
      Body(What w) : AST(w == STRUCT ? "struct_body" : "union_body") {}
      AST * part(unsigned i) {return members[i];}
      Vector<AST *> members;
      AST * parse_self(const Parse * p, ParseEnviron & env) {
        for (int i = 0; i != p->num_parts(); ++i) {
          add_ast_nodes(members, parse_member(p->part(i), env));
        }
        return this;
      }
      void compile(CompileWriter &, CompileEnviron &) {abort();}
    };
    StructUnion(What w) : AST(w == STRUCT ? "struct" : "union"), what(w)  {}
    AST * part(unsigned i) {return 0; /* FIXME */}
    String name;
    Body * body;
    ResolveEnviron env;
    AST * parse_self(const Parse * p, ParseEnviron & env0) {
      parse_ = p;
      assert(p->name == name_);
      name = p->arg(0)->name;
      env = env0.new_scope();
      if (p->num_args() > 1) {
        body = new Body(what);
        body->parse_self(p->arg(1), env);
      }
      const Type * t0 = env.types->inst(SymbolKey(TAG_NS,name));
      StructUnionT * s;
      if (t0) s = const_cast<StructUnionT *>(dynamic_cast<const StructUnionT *>(t0));
      else if (what == STRUCT) s = new StructT(name);
      else                     s = new UnionT(name);
      s->defined = true;
      for (unsigned i = 0; i != body->members.size(); ++i) {
	Var * v = dynamic_cast<Var *>(body->members[i]);
	assert(v);
	s->members.push_back(Member(v->name,v->var_type));
      }
      //StringBuf type_name;
      //type_name << "struct " << name_;
      s->env = &env;
      s->finalize();
      add_simple_type(env0.types, SymbolKey(TAG_NS, name), s);
      return this; 
    }
    void compile(CompileWriter & f, CompileEnviron &) {
      f << indent << name_ << " " << name << "{\n";
      for (int i = 0; i != body->members.size(); ++i) {
        f << adj_indent(2) << body->members[i];
      }
      f << indent << "};\n";
    }
  };

  struct Struct : public StructUnion {
    Struct() : StructUnion(STRUCT) {}
  };

  struct Union : public StructUnion {
    Union() : StructUnion(UNION) {}
  };

  struct Enum : public AST {
    Enum() : AST("enum") {}
    String name;
    struct Member {
      const Parse * parse;
      String name;
      Literal_Value<target_int> ct_value;
      Member(const Parse * p, String n, int v) : name(n), ct_value(v) {}
    };
    Vector<Member> members;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      name = p->arg(0)->name;
      EnumT * t0 = (const_cast<EnumT *>(dynamic_cast<const EnumT *>
                      (env.types->inst(SymbolKey(TAG_NS,name)))));
      if (!t0) t0 = new EnumT(name);
      t0->exact_type = env.types->inst("int")->exact_type;
      add_simple_type(env.types, SymbolKey(TAG_NS, name), t0);
      Vector<TypeParm> q_parms;
      q_parms.push_back(TypeParm(QualifiedType::CT_CONST));
      q_parms.push_back(TypeParm(t0));
      const Type * t = env.types->lookup(".qualified")->inst(q_parms);
      int val = 0;
      members.reserve(p->arg(1)->num_args());
      for (unsigned i = 0; i != p->arg(1)->num_args(); ++i) {
        if (p->arg(1)->arg(i)->num_parts() > 1) {
          AST * e = parse_exp(p->arg(1)->arg(i)->part(1), env);
          resolve_to(env, e, env.types->inst("int"));
          val = e->ct_value<target_int>();
        }
        Member mem(p->arg(1), p->arg(1)->arg(i)->part(0)->name, val);
        val++;
        Symbol * sym = new Symbol(mem.name);
        sym->type = t;
        members.push_back(mem);
        sym->ct_value = &members.back().ct_value;
        env.vars->root->add(mem.name, sym);
      }
      t0->finalize();
      return this;
    }
    void compile(CompileWriter & f, CompileEnviron &) {
      f << indent << name_ << " " << name << "{\n";
      for (int i = 0; i != members.size(); ++i) {
        f << adj_indent(2) << indent << members[i].name << " = " << members[i].ct_value.v;
        if (i == members.size())
          f << "\n";
        else
          f << ",\n";
      }
      f << indent << "};\n";
    }
  };

  struct SizeOf : public AST {
    SizeOf() : AST("sizeof") {}
    const Type * sizeof_type;
    AST * parse_self(const Parse * p, ParseEnviron & env) {
      parse_ = p;
      assert_num_args(1);
      type = env.types->ct_const(env.types->inst("unsigned"));
      sizeof_type = parse_type(env.types, p->arg(0), env);
      return this;
    }
    void compile(CompileWriter & f, CompileEnviron &) {
      StringBuf buf;
      c_print_inst->declaration("", *sizeof_type, buf);
      f << "sizeof(" << buf.freeze() << ")";
    }
  };

  AST * parse_top(const Parse * p) {
    ParseEnviron env;
    assert(p->name == "top");
    AST * res = (new Top)->parse_self(p, env);
    return res;
  }

  AST * try_decl(const Parse * p, ParseEnviron & env);
  AST * try_stmt(const Parse * p, ParseEnviron & env);
  AST * try_exp(const Parse * p, ParseEnviron & env);

  AST * parse_top_level(const Parse * p, ParseEnviron & env) {
    AST * res;
    res = try_decl(p, env);
    if (res) return res;
    //throw error (p, "Unsupported primative at top level: %s", ~p->name);
    throw error (p, "Expected top level expression.");
  }

  AST * parse_member(const Parse * p, ParseEnviron & env) {
    AST * res;
    res = try_decl(p, env);
    if (res) return res;
    //throw error (p, "Unsupported primitive inside a struct or union: %s", ~p->name);
    throw error (p, "Expected struct or union member.");
  }

  AST * parse_stmt(const Parse * p, ParseEnviron & env) {
    AST * res;
    res = try_stmt(p, env);
    if (res) return res;
    res = try_exp(p, env);
    if (res) return new EStmt(res);
    //throw error (p, "Unsupported primative at statement position: %s", ~p->name);
    throw error (p, "Expected statement.");
  }

  AST * parse_stmt_decl(const Parse * p, ParseEnviron & env) {
    AST * res;
    res = try_decl(p, env);
    if (res) return res;
    res = try_stmt(p, env);
    if (res) return res;
    res = try_exp(p, env);
    if (res) return new EStmt(res);
    //throw error (p, "Unsupported primative at statement position: %s", ~p->name);
    throw error (p, "Expected statement or declaration.");
  }

  AST * parse_exp(const Parse * p, ParseEnviron & env) {
    AST * res;
    res = try_exp(p, env);
    if (res) return res;
    //throw error (p, "Unsupported primative at expression position: %s", ~p->name);
    throw error (p, "Expected expression.");
  }

  AST * try_decl(const Parse * p, ParseEnviron & env) {
    if (p->name == "var")     return (new Var)->parse_self(p, env);
    if (p->name == "fun" )    return (new Fun)->parse_self(p, env);
    if (p->name == "struct")  return (new Struct)->parse_self(p, env);
    if (p->name == "union")   return (new Union)->parse_self(p, env);
    if (p->name == "enum")    return (new Enum)->parse_self(p, env);
    if (p->name == "talias")  return (new TypeAlias)->parse_self(p, env);
    if (p->name == "map")     return (new ASTList);
    if (p->name == "smap")    return (new ASTList);
    if (p->name == "slist")   return (new SList)->parse_self(p, env);
    return 0;
  }

  AST * try_stmt(const Parse * p, ParseEnviron & env) {
    if (p->name == "goto")    return (new Goto)->parse_self(p, env);
    if (p->name == "lstmt")   return (new LStmt)->parse_self(p, env);
    if (p->name == "lcstmt")  return (new LCStmt)->parse_self(p, env);
    if (p->name == "if")      return (new If)->parse_self(p, env);
    if (p->name == "loop")    return (new Loop)->parse_self(p, env);
    if (p->name == "switch")  return (new Switch)->parse_self(p, env);
    if (p->name == "break")   return (new Break)->parse_self(p, env);
    if (p->name == "block")   return (new Block)->parse_self(p, env);
    if (p->name == "print")   return (new Print)->parse_self(p, env);
    if (p->name == "noop")    return (new NoOp)->parse_self(p, env);
    if (p->name == "return")  return (new Return)->parse_self(p, env);
    return 0;
  }

  AST * try_exp(const Parse * p, ParseEnviron & env) {
    if (p->name == "id")      return (new Id)->parse_self(p, env);
    if (p->name == "literal") return (new Literal)->parse_self(p, env);
    if (p->name == "float")   return (new FloatC)->parse_self(p, env);
    if (p->name == "char")    return (new CharC)->parse_self(p, env);
    if (p->name == "string")  return (new StringC)->parse_self(p, env);
    if (p->name == "eif")     return (new EIf)->parse_self(p, env);
    if (p->name == "assign")  return (new Assign)->parse_self(p, env);
    if (p->name == "plus")    return (new Plus)->parse_self(p, env);
    if (p->name == "minus")   return (new Minus)->parse_self(p, env);
    if (p->name == "times")   return (new Times)->parse_self(p, env);
    if (p->name == "div")     return (new Div)->parse_self(p, env);
    if (p->name == "mod")     return (new Mod)->parse_self(p, env);
    if (p->name == "bor")     return (new BOr)->parse_self(p, env);
    if (p->name == "xor")     return (new XOr)->parse_self(p, env);
    if (p->name == "band")    return (new BAnd)->parse_self(p, env);
    if (p->name == "postinc") return (new PostInc)->parse_self(p, env);
    if (p->name == "postdec") return (new PostDec)->parse_self(p, env);
    if (p->name == "neg")     return (new Neg)->parse_self(p, env);
    if (p->name == "eq")      return (new Eq)->parse_self(p, env);
    if (p->name == "ne")      return (new Ne)->parse_self(p, env);
    if (p->name == "lt")      return (new Lt)->parse_self(p, env);
    if (p->name == "qt")      return (new Gt)->parse_self(p, env);
    if (p->name == "le")      return (new Le)->parse_self(p, env);
    if (p->name == "ge")      return (new Ge)->parse_self(p, env);
    if (p->name == "not")     return (new Not)->parse_self(p, env);
    if (p->name == "addrof")  return (new AddrOf)->parse_self(p, env);
    if (p->name == "deref")   return (new DeRef)->parse_self(p, env);
    if (p->name == "member")  return (new MemberAccess)->parse_self(p, env);
    if (p->name == "call")    return (new Call)->parse_self(p, env);
    if (p->name == "eblock")  return (new EBlock)->parse_self(p, env);
    if (p->name == "sizeof")  return (new SizeOf)->parse_self(p, env);
    if (p->name == "cast")    return (new ExplicitCast)->parse_self(p, env);
    if (p->name == "empty")   return (new Empty)->parse_self(p, env);
    if (strcmp(p->name + p->name.size()-3, "_eq") == 0) {
      // This is a bit of a hack to handle op_eq cases (ie += -=, etc)
      StringBuf buf(p->name, p->name.size()-3);
      AST * ast = try_exp(new Parse(p->str(), new Parse(buf.freeze()), p->arg(0), p->arg(1)), env);
      if (!ast) return 0;
      BinOp * binop = dynamic_cast<BinOp *>(ast);
      StringBuf op;
      op << binop->op << "=";
      return new CompoundAssign(p->name, op.freeze(), binop, env);
    }
    return 0;
  }

  template <>
  void CT_Value<uint8_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%llu", (unsigned long long)value(a));
  }

  template <>
  void CT_Value<uint16_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%llu", (unsigned long long)value(a));
  }

  template <>
  void CT_Value<uint32_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%llu", (unsigned long long)value(a));
  }

  template <>
  void CT_Value<uint64_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%llu", (unsigned long long)value(a));
  }

  template <>
  void CT_Value<int8_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%lld", (long long)value(a));
  }

  template <>
  void CT_Value<int16_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%lld", (long long)value(a));
  }

  template <>
  void CT_Value<int32_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%lld", (long long)value(a));
  }

  template <>
  void CT_Value<int64_t>::to_string(const AST * a, OStream & o) const {
    o.printf("%lld", (long long)value(a));
  }

  template <>
  void CT_Value<float>::to_string(const AST * a, OStream & o) const {
    o.printf("%f", value(a));
  }

  template <>
  void CT_Value<double>::to_string(const AST * a, OStream & o) const {
    o.printf("%f", value(a));
  }

  template <>
  void CT_Value<long double>::to_string(const AST * a, OStream & o) const {
    o.printf("%Lf", value(a));
  }

  //template <>
  //void CT_Value<bool>::to_string(const AST * a, OStream & o) const {
  //  if (value(a))
  //    o << '1';
  //  else
  //    o << '0';
  //}

}

