struct X {
  int num;
};

user_type X {
  static int last_num = 0;
  finalize_user_type struct X;
  macro num(:this this = this) {(*this)..num;}
  void constructor(X * fluid this) {
    num = last_num++;
    printf("HELLO %d\n", num);
  }
  macro _constructor(:this this) {constructor(this);}
  void destructor(X * fluid this) {
    printf("BY BY %d\n", num);
  }
  macro _destructor(:this this = this) {destructor(this);}
  void copy_constructor(X * fluid this, const X & x) {
    num = last_num++;
    printf("COPY %d = %d\n", num, x.num);
  }
  macro _copy_constructor(lhs, :this this  = this) {copy_constructor(this, lhs);}
  void assign(X * fluid this, const X & x) {
    printf("ASSIGN %d = %d\n", num, x.num);
  }
  macro _assign(lhs, :this this  = this) {assign(this, lhs);}
}

const X & x = X();

int main() {
  const X & x = X();
  X x2;
  x2 = x;
  x2 = X();
  return 0;
}

