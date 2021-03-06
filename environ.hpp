#include "symbol_table.hpp"
#include "type.hpp"
#include "abi_info.hpp"
#include "gc.hpp"

#ifndef ENVIRON_HPP
#define ENVIRON_HPP

struct PEG;

namespace ast {

  struct AST;
  struct Stmt;

  //class TypeSymbol;
  //class TypeInst;
  //typedef TypeInst Type;
  //struct Environ;
  //class TypeRelation;

  //enum Scope {STATIC, STACK};

  enum Scope {OTHER, TOPLEVEL, LEXICAL, EXTENDED_EXP};
  
  struct Frame : public gc {
    const TypeInst * return_type;
  };

  struct TopLevelVarDecl;
  typedef TopLevelVarDecl TopLevelVarSymbol;

  struct Deps : public Vector<const TopLevelVarSymbol *> {
    void insert(const TopLevelVarSymbol * sym) {
      for (iterator i = begin(), e = end(); i != e; ++i)
        if (*i == sym) return;
      push_back(sym);
    }
    void merge(const Deps & other) {
      for (const_iterator i = other.begin(), e = other.end(); i != e; ++i)
        insert(*i);
    }
    bool have(const TopLevelVarSymbol * sym) {
      for (const_iterator i = begin(), e = end(); i != e; ++i)
        if (*i == sym) return true;
      return false;
    }
  };

  struct InsrPoint {
    InsrPoint(Stmt * * p = NULL) : ptr(p) {}
    Stmt * * ptr; // insertion point
    inline void add(Stmt *); // defined in ast.hpp
    inline void add(Exp *); // defined in ast.hpp
    operator bool () const {return ptr;}
    bool operator! () const {return !ptr;}
    void operator=(Stmt * * p) {ptr = p;}
  };

  struct ExpInsrPoint : public InsrPoint {
    enum Where {ExtendedExp, Var, TopLevelVar};
    ExpInsrPoint(Stmt * * p, Where w = ExtendedExp) : InsrPoint(p), where(w) {}
    Where where;
  };

  struct CollectAction {
    Environ * env;
    CollectAction(Environ & e) : env(&e) {}
    // perform action, but first fix up the cached env.
    inline void doit(Environ & e);
    // perform object specific action
    virtual void doit_hook() = 0;
    virtual ~CollectAction() {}
  };

  register void * STACK_PTR asm ("esp");
#define ON_STACK(ptr) ((void *)ptr >= STACK_PTR)

  struct CollectPart : public Vector<CollectAction * > {
    CollectPart() {}
    void add(CollectAction * action) {
      assert(action);
      assert(ON_STACK(this) || !ON_STACK(action->env));
      push_back(action);
    }
  };

  struct Collect {
    CollectPart first_pass;
    CollectPart second_pass;
  };

  void add_ast_primitives(Environ &);

  // Yeah, a bit of a hack should't really be global....
  // anyway defined in expand.cpp
  extern Vector<AbiInfo> abi_list;
  extern AbiInfo DEFAULT_ABI_INFO;
  AbiInfo * get_abi_info(String name);

  struct Environ : public gc {
    TypeRelation * type_relation;
    bool special() const {return !top_level_symbols;}
    bool parse_def() const {return top_level_symbols && !interface;}
    const Symbol * find_tls(const char * to_find) const {
      //if (!top_level_symbols)
      //  return NULL;
      return top_level_symbols->find<Symbol>(to_find);
    }
    inline void add_defn(Stmt * defn);
    inline void move_defn(Stmt * defn);
    TopLevelSymbolTable * top_level_symbols;
    SymbolTable symbols;
    
    TypeSymbolTable types;
    SymbolInsrPoint fun_labels;
    Scope scope;
    Frame * frame;
    TopLevelSymbol * where;
    SymbolNode * const * top_level_environ;
    Deps * deps;
    bool * for_ct; // set if this function uses a ct primitive such as syntax
    InsrPoint * stmt_ip;
    ExpInsrPoint * temp_ip;
    InsrPoint * exp_ip;
    Collect * collect; // if set don't parse declaration body,
                       // instead store it here to be parsed latter
    AbiInfo * abi_info;
    PEG * peg;
    bool true_top_level;
    bool interface;
    bool mangle;
    bool link_once;
    Type * void_type() {return types.inst("<void>");}
    //Type * bool_type() {return types.inst("<bool>");}
    Type * bool_type() {return types.inst("int");}
    FunctionSymbol * function_sym() 
      {return static_cast<FunctionSymbol *>(types.find(".fun"));}
    Environ(Scope s, PEG * p) 
      : types(this), scope(s), where(),
        top_level_environ(&symbols.front), 
        deps(), for_ct(), temp_ip(), exp_ip(), collect(), 
        abi_info(&DEFAULT_ABI_INFO), peg(p), 
        true_top_level(false), interface(false), mangle(true), link_once(false)
      {
        if (s == TOPLEVEL) {
          true_top_level = true;
          top_level_symbols = new TopLevelSymbolTable(&symbols.front);
          add_ast_primitives(*this); // FIXME HACK
          type_relation = new_c_type_relation(); // FIXME HACK
          create_c_types(types); // FIXME Another HACK
          add_inner_nss(*this);
        }
        frame = new Frame();
      }
    Environ(const Environ & other) 
      : type_relation(other.type_relation), 
        top_level_symbols(other.top_level_symbols), 
        symbols(other.symbols),
        types(this), fun_labels(other.fun_labels), 
        scope(other.scope), frame(other.frame), 
        where(other.where),
        top_level_environ(other.top_level_environ == &other.symbols.front ? &symbols.front :  other.top_level_environ),
        deps(other.deps), for_ct(other.for_ct), 
        stmt_ip(other.stmt_ip), temp_ip(other.temp_ip), exp_ip(other.exp_ip), 
        collect(other.collect), abi_info(other.abi_info), peg(other.peg),
        true_top_level(other.true_top_level), interface(other.interface), 
        mangle(other.mangle), link_once(other.link_once) {}
    Environ new_scope() const {
      Environ env = *this;
      env.true_top_level = false;
      env.stmt_ip = NULL;
      env.collect = NULL;
      env.symbols = symbols.new_scope();
      env.link_once = false;
      return env;
    }
    Environ new_open_scope() const {
      Environ env = *this;
      env.true_top_level = false;
      env.stmt_ip = NULL;
      env.collect = NULL;
      env.symbols = symbols.new_open_scope();
      env.link_once = false;
      return env;
    }
    Environ new_extended_exp(ExpInsrPoint * ip, bool force_new_scope) const {
      Environ env = *this;
      env.true_top_level = false;
      if (force_new_scope || !env.temp_ip) {
        env.temp_ip = ip;
        env.exp_ip = ip;
      }
      return env;
    }
    Environ new_exp_branch(InsrPoint * ip) const { // used for "eif"
      Environ env = *this;
      assert(!env.true_top_level);
      env.exp_ip = ip;
      return env;
    }

    void add_stmt(Stmt * stmt) {
      stmt_ip->add(stmt);
    }

    SymbolNode * add(const SymbolKey & k, Symbol * sym, bool shadow_ok = false) {
      return sym->add_to_env(k, *this, shadow_ok);
    }

    SymbolNode * add_internal(const SymbolKey & k, Symbol * sym) {
      SymbolNode * n = symbols.add(NULL, k, sym, SymbolNode::INTERNAL);
      sym->key = &n->key;
      return n;
    }

    SymbolNode * add_alias(const SymbolKey & k, Symbol * sym) {
      //if (true_top_level)
      //  top_level_symbols->add_top_level(k, sym, SymbolNode::ALIAS);
      //else
      return symbols.add(where, k, sym, SymbolNode::ALIAS);
    }

    Environ new_frame() {
      Environ env = *this;
      env.true_top_level = false;
      env.stmt_ip = NULL;
      env.collect = NULL;
      env.scope = LEXICAL;
      env.symbols = symbols.new_scope(env.fun_labels);
      env.frame = new Frame();
      env.top_level_environ = &symbols.front;
      env.for_ct = NULL;
      env.link_once = false;
      return env;
    }
  private:
  };

  inline void CollectAction::doit(Environ & e) {
    // fix up local env so it is no longer temp.
    env->top_level_symbols = e.top_level_symbols;

    doit_hook();
  }
  
  inline TypeSymbol * TypeSymbolTable::find(const SymbolKey & k) {
    return env->symbols.find<TypeSymbol>(k);
  }

  inline TypeSymbol * TypeSymbolTable::find(const Syntax * p, const InnerNS * ns) {
    return env->symbols.find<TypeSymbol>(p, ns);
  }

  inline SymbolNode * TypeSymbolTable::add(const SymbolKey & k, TypeSymbol * t) {
    return env->add(k, t);
  }

  inline SymbolNode * TypeSymbolTable::add_internal(const SymbolKey & k, TypeSymbol * t) {
    return env->add_internal(k, t);
  }

  inline SymbolNode * TypeSymbolTable::add_alias(const SymbolKey & k, TypeSymbol * t) {
    return env->add_alias(k, t);
  }
}

#endif
