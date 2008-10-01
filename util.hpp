#ifndef UTIL__HPP
#define UTIL__HPP

#include <assert.h>
#include <stdlib.h>

#include "gc.hpp"
#include "vector.hpp"
#include "parm_string.hpp"

struct StringObj {
  unsigned size;
  char str[];
};

extern const StringObj * const EMPTY_STRING_OBJ;

struct SubStr;

struct String {
private:
  const StringObj * d;
public:
  typedef const char * iterator;
  typedef const char * const_iterator;

  void assign(const char * str, unsigned sz) {
    StringObj * d0 = (StringObj *)GC_MALLOC_ATOMIC(sizeof(StringObj) + sz + 1);
    d0->size = sz;
    memmove(d0->str, str, sz);
    d0->str[sz] = '\0';
    d = d0;
  }
  void assign(const char * str, const char * e) {
    assign(str, e - str);
  }
  inline void assign(const SubStr & str);
  void assign(const char * str) {assign(str, strlen(str));}
  String() : d(EMPTY_STRING_OBJ) {}
  String(const SubStr & str) {assign(str);}
  String(const char * str) {assign(str);}
  String(const StringObj * str) : d(str) {assert(str);}
  String(const char * str, const char * e) {assign(str, e);}
  bool defined() const {return d != EMPTY_STRING_OBJ;}
  unsigned size() const {return d->size;}
  bool empty() const {return d->size == 0;}
  const char * begin() const {return d->str;}
  const char * end()   const {return d->str + d->size;}
  const char * data() const {return d->str;}
  const char * pbegin() const {return d->str;}
  const char * pend()   const {return d->str + d->size;}
  char operator[] (unsigned sz) const {return d->str[sz];}
  char operator[] (int sz)      const {return d->str[sz];}
  operator const char * () const {return d->str;}
  const char * c_str() const {return d->str;}
  const char * operator~() const {return d->str;}
};

inline ParmString::ParmString(String s) 
  : str_(s), size_(s.size()) {}

struct SubStr {
  const char * begin;
  const char * end;
  SubStr() 
    : begin(0), end(0) {}
  SubStr(const char * b, const char * e) : begin(b), end(e) {}
  SubStr(String s) : begin(s.pbegin()), end(s.pend()) {}
  //explicit SubStr(const string & s) : begin(s.c_str()), end(begin + s.size()) {}
  void clear() {begin = end = 0;}
  void assign(const char * b, const char * e) {begin = b; end = e;}
  bool empty() const {return begin == end;}
  unsigned size() const {return end - begin;}
  operator const char * () {return begin;}
  SubStr & operator=(const char * s) {begin = s; return *this;}
};

inline void String::assign(const SubStr & str) {assign(str.begin, str.size());}

int cmp(const char * x, unsigned x_sz, const char * y, unsigned y_sz);

#define OP_CMP(T1, T2) \
  static inline bool operator==(T1 x, T2 y) {\
    if (x.size() != y.size()) return false;\
    return memcmp(x, y, x.size()) == 0;\
  }\
  static inline bool operator!=(T1 x, T2 y) {\
    if (x.size() != y.size()) return true;\
    return memcmp(x, y, x.size()) != 0;\
  }\
  static inline int cmp(T1 x, T2 y) {\
    return cmp(x, x.size(), y, y.size()); \
  }\
  static inline bool operator<(T1 x, T2 y) {\
    return cmp(x, x.size(), y, y.size()) < 0;\
  }
OP_CMP(String, String);
OP_CMP(SubStr, SubStr);
OP_CMP(String, SubStr);
OP_CMP(SubStr, String);

#define CSTR_OP_CMP(T1, T2) \
  static inline bool operator==(T1 x, T2 y) {return strcmp(x, y) == 0;}\
  static inline bool operator!=(T1 x, T2 y) {return strcmp(x, y) != 0;}\
  static inline int cmp(T1 x, T2 y) {return strcmp(x,y);} \
  static inline bool operator<(T1 x, T2 y) {return strcmp(x,y) < 0;}\
  
CSTR_OP_CMP(String, const char *);
CSTR_OP_CMP(const char *, String);

struct Pos {
  unsigned line;
  unsigned col;
  Pos(unsigned l, unsigned c) : line(l), col(c) {}
};

char * pos_to_str(Pos p, char * buf);

class OStream;
class SourceFile;

#define PURE __attribute__ ((pure)) 

//
// A SourceInfo "block" is part of a "file".  The syntax inside a
// macro call or syntax primitive is its own block.  If a piece
// of syntax contains two parts from the same block the the SourceStr
// for that syntax is considered the "bounding box" of its parts.
//
class SourceInfo : public gc_cleanup {
public:
  String file_name() const;
  Pos get_pos(const char * s) const;
  unsigned size() const;
  const char * begin() const;
  const char * end() const;    
  PURE virtual const SourceFile * file() const = 0;
  PURE virtual const SourceInfo * block() const = 0; // FIXME: Need better name
  PURE virtual const SourceInfo * parent() const = 0;
  virtual void dump_info(OStream &, const char * prefix="") const = 0;
  virtual ~SourceInfo() {}
};

class SourceFile : public SourceInfo {
private:
  String file_name_;
  char * data_;
  unsigned size_;
  Vector<const char *> lines_;
public:
  SourceFile(String file) : data_(), size_(0) {read(file);}
  SourceFile(int fd) : data_(), size_(0) {read(fd);}
  String file_name() const {return file_name_;}
  Pos get_pos(const char * s) const;
  char * get_pos_str(const char * s, char * buf) const {
    return pos_to_str(get_pos(s), buf);
  }
  unsigned size() const {return size_;}
  const char * begin() const {return data_;}
  const char * end() const {return data_ + size_;}
  ~SourceFile() {if (data_) free(data_);}
  const SourceFile * file() const {return this;}
  const SourceInfo * block() const {return this;}
  const SourceInfo * parent() const {return NULL;}
  void dump_info(OStream & o, const char * prefix) const;
private:
  void read(String file);
  void read(int fd);
};

inline String SourceInfo::file_name() const {return file()->file_name();}
inline Pos SourceInfo::get_pos(const char * s) const {return file()->get_pos(s);}
inline unsigned SourceInfo::size() const {return file()->size();}
inline const char * SourceInfo::begin() const {return file()->begin();}
inline const char * SourceInfo::end() const {return file()->end();}

SourceFile * new_source_file(String file);
SourceFile * new_source_file(int fd);

#undef NPOS
static const unsigned NPOS = UINT_MAX;

#endif
