#include "type.hpp"
#include "parse.hpp"
#include "ast.hpp"

namespace AST {

  static TypeCategory ANY_C_OBJ("any", 0);
  static TypeCategory SCALAR_C_OBJ("scalar", ANY_C);
  static TypeCategory NUMERIC_C_OBJ("numeric", SCALAR_C);
  static TypeCategory INT_C_OBJ("integer", NUMERIC_C);
  static TypeCategory SIGNED_C_OBJ("signed integer", INT_C);
  static TypeCategory UNSIGNED_C_OBJ("unsigned integer", INT_C);
  static TypeCategory FLOAT_C_OBJ("floating point", NUMERIC_C);
  static TypeCategory POINTER_C_OBJ("pointer", SCALAR_C);
  static TypeCategory ARRAY_C_OBJ("array", POINTER_C); // FIXME: Is this right?
  static TypeCategory FUN_C_OBJ("function", ANY_C); // FIXME: Is this right?
  static TypeCategory UNKNOWN_C_OBJ("?", ANY_C);
  static TypeCategory ZERO_C_OBJ("zero", INT_C, POINTER_C);

  TypeCategory * const ANY_C = &ANY_C_OBJ;
  TypeCategory * const SCALAR_C = &SCALAR_C_OBJ;
  TypeCategory * const NUMERIC_C = &NUMERIC_C_OBJ;
  TypeCategory * const INT_C = &INT_C_OBJ;
  TypeCategory * const SIGNED_C = &SIGNED_C_OBJ;
  TypeCategory * const UNSIGNED_C = &UNSIGNED_C_OBJ;
  TypeCategory * const FLOAT_C = &FLOAT_C_OBJ;
  TypeCategory * const POINTER_C = &POINTER_C_OBJ;
  TypeCategory * const ARRAY_C = &ARRAY_C_OBJ;
  TypeCategory * const FUN_C = &FUN_C_OBJ;
  TypeCategory * const UNKNOWN_C = &UNKNOWN_C_OBJ;
  TypeCategory * const ZERO_C = &ZERO_C_OBJ;

  Type * VOID_T = 0;

  const char * type_name(const Type * t) {
    return typeid(*t).name();
  }

  Type * TypeSymbolTable::inst(SymbolKey n, Vector<TypeParm> & p) {
    if (!exists(n)) return 0;
    const TypeSymbol * s = lookup(n);
    return s->inst(p);
  }

  void TypeSymbolTable::add_name(SymbolKey k, TypeSymbol * t) {
    t->name = k.second;
    Base::add(k,t);
  }

  PrintInst const * const generic_print_inst = new GenericPrintInst();
  PrintInst const * const c_print_inst = new CPrintInst();
  
  void TypeParm::to_string(StringBuf & buf) const {
    switch (what) {
    case NONE:
      buf << "<none>";
      break;
    case TYPE:
    case TUPLE:
      as_type->to_string(buf);
      break;
    case INT:
      buf.printf("%d", as_int);
      break;
    case EXP:
      buf.printf("<EXP>");
      break;
    case DOTS:
      buf.printf("...");
      break;
    }
  }

  bool operator==(const TypeInst & lhs, const Vector<TypeParm> & rhs) {
    unsigned lhs_sz = lhs.num_parms(), rhs_sz = rhs.size();
    if (lhs_sz != rhs_sz) return false;
    for (unsigned i = 0; i != lhs_sz; ++i) {
      if (lhs.parm(i) != rhs[i]) return false;
    }
    return true;
  }

  bool operator==(const TypeInst & lhs, const TypeInst & rhs) {
    if (lhs.type_symbol != rhs.type_symbol) return false;
    unsigned lhs_sz = lhs.num_parms(), rhs_sz = rhs.num_parms();
    if (lhs_sz != rhs_sz) return false;
    for (unsigned i = 0; i != lhs_sz; ++i) {
      if (lhs.parm(i) != rhs.parm(i)) return false;
    }
    return true;
  }

  void GenericPrintInst::to_string(const TypeInst & type, StringBuf & buf) const {
    unsigned sz = type.num_parms();
    if (sz == 0) {
      buf += type.type_symbol->name;
    } else {
      buf += "(";
      buf += type.type_symbol->name;
      buf += " ";
      for (unsigned i = 0;;) {
        type.parm(i).to_string(buf);
        ++i;
        if (i == sz) break;
        buf += " ";
      }
      buf += ")";
    }
  }

  void GenericPrintInst::declaration(String var, const TypeInst & type, StringBuf & buf) const {
    buf << "var ";
    buf << var << " : ";
    to_string(type, buf);
  }

  void CPrintInst::to_string(const TypeInst & type, StringBuf & buf) const {
    declaration("", type, buf);
  }

  void CPrintInst::declaration(String var, const TypeInst & type0, StringBuf & buf, bool parentheses) const {
    const Type * type = &type0;
    String qualifiers;
    for (;;) { // loop while something changed
      if (const ZeroT * t = dynamic_cast<const ZeroT *>(type)) {
        type = t->of;
      } else if (const QualifiedType * t = dynamic_cast<const QualifiedType *>(type)) {
        StringBuf lbuf = qualifiers;
        if (t->qualifiers & QualifiedType::CONST)    lbuf += " const";
        if (t->qualifiers & QualifiedType::VOLATILE) lbuf += " volatile";
        if (t->qualifiers & QualifiedType::RESTRICT) lbuf += " restrict";
        qualifiers = lbuf.freeze();
        type = t->subtype;
      } else if (const TypeOf * t = dynamic_cast<const TypeOf *>(type)) {
        type = t->of;
      } else {
        break;
      }
    }
    if (const Tuple * t = dynamic_cast<const Tuple *>(type)) {
      buf << "(";
      for (unsigned i = 0; i < t->parms.size();) {
        declaration(t->parms[i].name, *t->parms[i].type, buf);
        ++i;
        if (i == t->parms.size()) break;
        buf << ", ";
      }
      if (t->vararg) {
	buf << ", ...";
      }
      buf << ")";
    } else if (const Pointer * t = dynamic_cast<const Pointer *>(type)) {
      const Type * rec_subtype = t->subtype;
      while (const Pointer * t2 = dynamic_cast<const Pointer *>(rec_subtype))
        rec_subtype = t2->subtype;
      StringBuf lbuf;
      lbuf << "*" << qualifiers;
      if (!var.empty())
        lbuf << " " << var;
      declaration(lbuf.freeze(), *t->subtype, buf, true);
    } else if (const Array * t = dynamic_cast<const Array *>(type)) {
      assert(qualifiers.empty());
      StringBuf lbuf;
      if (parentheses) lbuf << "(" << var << ")";
      else             lbuf << var;
      lbuf.printf("[%d]", t->length);
      declaration(lbuf.freeze(), *t->subtype, buf);
    } else if (const FunctionPtr * t = dynamic_cast<const FunctionPtr *>(type)) {
      assert(qualifiers.empty());
      StringBuf lbuf;
      if (parentheses) lbuf << "(" << var << ")";
      else             lbuf << var;
      declaration("", *t->parms, lbuf);
      declaration(lbuf.freeze(), *t->ret, buf);
    } else if (type->num_parms() == 0) {
      if (const TaggedType * t = dynamic_cast<const TaggedType *>(type)) {
        buf << t->what << " ";
      }
      buf << type->type_symbol->name << qualifiers;
      if (!var.empty())
	buf << " " << var;
    } else {
      abort();
    }
  }

  Type * TypeOfSymbol::inst(Vector<TypeParm> & d) const {
    assert(d.size() == 1);
    assert(d[0].what == TypeParm::EXP);
    TypeOf * t = new TypeOf(d[0].as_exp);
    t->finalize();
    return t;
  }

  inline TypeOf::TypeOf(AST * a) 
    : SimpleTypeInst(a->type), of_ast(a), of(a->type) {type_symbol = of->type_symbol;}

  Type * parse_type(TypeSymbolTable * types, const Parse * p, ParseEnviron & env) {
    unsigned sz = p->num_args();
    const NameSpace * ns = DEFAULT_NS;
    String name = p->name;
    String full_name = name;
    String tag;
    if (name == "struct" || name == "union" || name == "enum") {
      tag = name;
      assert(sz == 1);
      // FIXME: Add check for wrong type of tag
      ns = TAG_NS;
      name = p->arg(0)->name;
      StringBuf buf;
      buf << p->name << " " << p->arg(0)->name;
      full_name = buf.freeze();
      --sz;
    }
    if (name == ".typeof") {
      AST * ast = parse_exp(p->arg(0), env);
      Type * t = new TypeOf(ast);
      t->finalize();
      return t;
    }
    const TypeSymbol * t = types->lookup(SymbolKey(ns,name));
    if (!t) {
      if (tag.empty()) {
        throw error(p, "Unknown type: %s", ~full_name);
      } else {
        SimpleTypeInst * ti;
        if (tag == "struct")     ti = new StructT(name);
        else if (tag == "union") ti = new UnionT(name);
        else if (tag == "enum")  ti = new EnumT(name);
        else abort();
        add_simple_type(types, SymbolKey(TAG_NS, name), ti);
        t = types->lookup(SymbolKey(TAG_NS,name));
        assert(t);
      }
    }
    if (t->required_parms() == 0 && sz != 0 && name != ".tuple" /* HACK! */)
      throw error(p, "Type \"%s\" is not a paramatized type.", ~full_name);
    if (t->required_parms() > sz) 
      throw error(p, "Not enough paramaters for \"%s\" type, %d required, but got %d",
                  ~full_name, t->required_parms(), sz);
    Vector<TypeParm> parms;
    parms.reserve(sz);
    for (int i = 0; i != sz; ++i) {
      const Parse * p0 = p->arg(i);
      String n;
      if (name == ".tuple" && !p0->part(0)->simple()) { // HACK!
	if (p0->num_parts() > 1)
	  n = p0->part(1)->name;
	p0 = p0->part(0);
      }
      switch(t->parm(i)) {
      case TypeParm::TYPE: {
	if (p0->name == "...") {
	  parms.push_back(TypeParm::dots());
	} else {
	  Type * t0 = parse_type(types, p0, env);
	  parms.push_back(TypeParm(t0, n));
	}
        break;
      }
      case TypeParm::INT: {
        AST * exp = parse_exp(p0, env);
        resolve_to(env, exp, types->inst("int"));
        parms.push_back(TypeParm(exp->ct_value<int>()));
        break;
      }
      case TypeParm::TUPLE: {
        Type * t0 = parse_type(types, p0, env);
        Tuple * tt = dynamic_cast<Tuple *>(t0);
        if (!tt)
          throw error(p0, "Expected Tuple Type");
        parms.push_back(TypeParm(TypeParm::TUPLE, t0));
        break;
      }
      case TypeParm::EXP: {
        AST * exp = parse_exp(p0, env);
        parms.push_back(TypeParm(exp));
        break;
      }
      case TypeParm::NONE: {
        throw error(p0, "Two Many Type Paramaters");
      }
      case TypeParm::DOTS: {
	abort(); // special case, should not happen
      }}
    }
    Type * inst_type = t->inst(parms);
    unsigned qualifiers = 0;
    if (p->flag("const"))    qualifiers |= QualifiedType::CONST;
    if (p->flag("volatile")) qualifiers |= QualifiedType::VOLATILE;
    if (p->flag("restrict")) qualifiers |= QualifiedType::RESTRICT;
    if (qualifiers) {
      Vector<TypeParm> q_parms;
      q_parms.push_back(TypeParm(qualifiers));
      q_parms.push_back(TypeParm(inst_type));
      return types->lookup(".qualified")->inst(q_parms);
    } else {
      return inst_type;
    }
  }
  
  FunctionPtr * FunctionPtrSymbol::inst(TypeSymbolTable * types, Fun * f) const {
    Vector<TypeParm> p;
    p.clear();
    p.push_back(TypeParm(TypeParm::TUPLE, f->parms));
    p.push_back(TypeParm(f->type));
    return static_cast<FunctionPtr *>(inst(p));
  }
  

#if 0
  FunctionPtr::FunctionPtr(Fun * f) 
    : ParmTypeInst(POINTER_SIZE)
  {
    ret = f->type;
    Tuple * ps = new Tuple();
    parms = ps;
    for (int i = 0; i != f->parms.size(); ++i) {
      ps->parts.push_back(f->parms[i].type);
    }
  }
#endif

  const Type * TypeRelation::unify(int rule, AST * & x, AST * & y) {
    const Type * xt = x->type->unqualified;
    const Type * yt = y->type->unqualified;
    const Type * t = unify(rule, xt, yt);
    //printf("UNIFY %d to \"\%s\"\n", x->parse_->str().source->get_pos(x->parse_->str().begin).line, ~t->to_string());
    if (t != xt) {x = new ImplicitCast(x, t);}
    if (t != yt) {y = new ImplicitCast(y, t);}
    return t;
  }

  class C_TypeRelation : public TypeRelation {
  public:
    AST * resolve_to(AST * exp, const Type * type) const;
    const Type * unify(int rule, const Type *, const Type *) const;
    AST * cast(int rule, AST * exp, const Type * type) const;
  };

  AST * C_TypeRelation::resolve_to(AST * exp, const Type * type) const {
    static int i = -1;
    ++i;
    const Type * have = exp->type->unqualified;
    const Type * need = type->unqualified;

    if (have == need) return exp;
    // FIXME: Is this right?

    if (dynamic_cast<const FunctionPtr *>(have) 
             && dynamic_cast<const FunctionPtr *>(need))
      return exp;

    if (have->is(NUMERIC_C) && need->is(NUMERIC_C))
      return new ImplicitCast(exp, type); 

    // deal with pointer types
    {
      const Pointer * n_p = dynamic_cast<const Pointer *>(need);
      const Type * n_subtype = n_p ? n_p->subtype : 0;
      if (!n_subtype) goto fail;
      if (exp->type->is_null) return exp;
      const Pointer * h_p = dynamic_cast<const Pointer *>(have);
      const Array   * h_a = dynamic_cast<const Array *>(have);
      const Type * h_subtype = h_p ? h_p->subtype : h_a ? h_a->subtype : 0;
      if (!h_subtype) goto fail;
      if (h_subtype->unqualified == n_subtype->unqualified ||
          dynamic_cast<const Void *>(h_subtype->unqualified) ||
          dynamic_cast<const Void *>(n_subtype->unqualified) /* this one is C only */)  
      {
        if (h_subtype->read_only && !n_subtype->read_only)
          throw error(exp->parse_, "Conversion from \"%s\" to \"%s\" disregards const qualifier\n", 
                      ~have->to_string(), ~need->to_string());
        else
          return exp;
      } else {
        goto fail;
      }
    }
    //abort();

  fail:
    throw error(exp->parse_, "%d Mismatch Types expected \"%s\" but got \"%s\"", i,
                ~type->to_string(), ~exp->type->to_string());
  }

  const Type * C_TypeRelation::unify(int rule, const Type * x, const Type * y) const {
    // 3.6.1.8: Usual arithmetic conversions
    x = x->unqualified;
    y = y->unqualified;
    if (x == y) return x;
    {
      const Float * x0 = dynamic_cast<const Float *>(x);
      const Float * y0 = dynamic_cast<const Float *>(y); 
      if (x0 && y0) return x0->precision > y0->precision ? x0 : y0;
      if (x0) return x0;
      if (y0) return y0;
    } {
      const Int * x0 = dynamic_cast<const Int *>(x);
      const Int * y0 = dynamic_cast<const Int *>(x);
      assert(x0 && y0);
      if (x0->signed_ == y0->signed_) return x0->rank > y0->rank ? x0 : y0;
      const Int * u = x0->signed_ == Int::UNSIGNED ? x0 : y0;
      const Int * s = x0->signed_ == Int::SIGNED   ? x0 : y0;
      assert (u != s);
      if (u->rank >= s->rank) return u;
      if (s->min <= u->min && s->max >= u->max) return s;
      abort(); // FIXME: From standard: Otherwise, both operands are
               // converted to the unsigned integer type corresponding
               // to the type of the operand with signed integer type.

    }
  }

  AST * C_TypeRelation::cast(int rule, AST * exp, const Type * type) const {
    return exp; // FIXME
  }

  TypeRelation * new_c_type_relation() {
    return new C_TypeRelation();
  }

  void add_c_int(TypeSymbolTable * types, String n);

  void create_c_types(TypeSymbolTable * types) {
    add_simple_type(types, ".int8", new_signed_int(1));
    add_simple_type(types, ".uint8", new_unsigned_int(1));
    add_simple_type(types, ".int16", new_signed_int(2));
    add_simple_type(types, ".uint16", new_unsigned_int(2));
    add_simple_type(types, ".int32", new_signed_int(4));
    add_simple_type(types, ".uint32", new_unsigned_int(4));
    add_simple_type(types, ".int64", new_signed_int(8));
    add_simple_type(types, ".uint64", new_unsigned_int(8));

    add_c_int(types, "signed char");
    add_c_int(types, "short");
    add_c_int(types, "int");
    add_c_int(types, "long");
    add_c_int(types, "long long");

    //types->add("signed short", types->lookup("short"));
    //types->add("signed int", types->lookup("int"));
    //types->add("signed long", types->lookup("long"));
    //types->add("signed long long", types->lookup("long long"));

    add_c_int(types, "unsigned char");
    add_c_int(types, "unsigned short");
    add_c_int(types, "unsigned int");
    add_c_int(types, "unsigned long");
    add_c_int(types, "unsigned long long");

    types->add("unsigned", types->lookup("unsigned int"));

    add_c_int(types, "char");

    VOID_T = new Void();
    add_simple_type(types, "void", static_cast<SimpleTypeInst *>(VOID_T));

    add_simple_type(types, "float", new Float(Float::SINGLE));
    add_simple_type(types, "double", new Float(Float::DOUBLE));
    add_simple_type(types, "long double", new Float(Float::LONG));

    add_simple_type(types, "<bool>", new AliasT(types->inst("int")));

    types->add("<void>", types->lookup("void"));

    types->add_name(".pointer", new PointerSymbol);
    //types->add_name("<const>", new ConstSymbol);
    types->add_name(".array", new ArraySymbol);
    types->add_name(".fun", new FunctionPtrSymbol);
    types->add_name(".tuple", new TupleSymbol);
    types->add_name(".qualified", new QualifiedTypeSymbol);
    types->add_name(".zero", new ZeroTypeSymbol);
    types->add_name(".typeof", new TypeOfSymbol);
  }

  Type * TypeSymbolTable::ct_const(const Type * t) {
    Vector<TypeParm> parms;
    parms.push_back(TypeParm(QualifiedType::CT_CONST));
    parms.push_back(TypeParm(t));
    return inst(".qualified", parms);
  }

}

namespace AST {

  struct IntMap {
    String c_type;
    String exact_type;
  };

  const IntMap int_map[] = {
    {"char", ".int8"},
    {"signed char", ".int8"},
    {"unsigned char", ".uint8"},
    {"short", ".int16"},
    {"unsigned short", ".uint16"},
    {"int", ".int32"},
    {"unsigned int", ".uint32"},
    {"long", ".int32"},
    {"unsigned long", ".uint32"},
    {"long long", ".int64"},
    {"unsigned long long", ".uint64"}
  };
  const IntMap * int_map_end = int_map + sizeof(int_map)/sizeof(IntMap);
  
  String c_type_to_exact(String t) {
    for (const IntMap * i = int_map; i != int_map_end; ++i) {
      if (i->c_type == t) return i->exact_type;
    }
    abort();
  }
  
  void add_c_int(TypeSymbolTable * types, String n) {
    add_simple_type(types, n, new Int(static_cast<const Int *>(types->inst(c_type_to_exact(n)))));
  }

}

