// RUN: %clang_cc1 -std=c++11 -triple=amdgcn-amd-amdhsa -emit-llvm -fexceptions %s -o - |FileCheck %s
// RUN: %clang_cc1 -std=c++11 -triple=amdgcn-amd-amdhsa -emit-llvm %s -o - |FileCheck -check-prefix CHECK-NOEXC %s
// RUN: %clang_cc1 -std=c++11 -triple=amdgcn-amd-amdhsa -emit-llvm \
// RUN:     -momit-leaf-frame-pointer -mdisable-fp-elim %s -o - \
// RUN:   | FileCheck -check-prefix CHECK-FP %s

struct A {
  A();
  ~A();
};

struct B { B(); ~B(); };

struct C { void *field; };

struct D { ~D(); };

// CHECK: @__dso_handle = external hidden addrspace(1) global i8
// CHECK: @c = addrspace(1) global %struct.C zeroinitializer, align 8

// PR6205: The casts should not require global initializers
// CHECK: @_ZN6PR59741cE = external addrspace(1) global %"struct.PR5974::C"
// CHECK: @_ZN6PR59741aE = addrspace(1) global %"struct.PR5974::A"* addrspacecast (%"struct.PR5974::A" addrspace(1)* getelementptr inbounds (%"struct.PR5974::C", %"struct.PR5974::C" addrspace(1)* @_ZN6PR59741cE, i32 0, i32 0) to %"struct.PR5974::A"*), align 8
// CHECK: @_ZN6PR59741bE = addrspace(1) global %"struct.PR5974::B"* bitcast (i8* getelementptr (i8, i8* addrspacecast (i8 addrspace(1)* bitcast (%"struct.PR5974::C" addrspace(1)* @_ZN6PR59741cE to i8 addrspace(1)*) to i8*), i64 4) to %"struct.PR5974::B"*), align 8

// CHECK: call void @_ZN1AC1Ev(%struct.A* addrspacecast (%struct.A addrspace(1)* @a to %struct.A*))
// CHECK: call i32 @__cxa_atexit(void (i8*)* bitcast (void (%struct.A*)* @_ZN1AD1Ev to void (i8*)*), i8* addrspacecast (i8 addrspace(1)* getelementptr inbounds (%struct.A, %struct.A addrspace(1)* @a, i32 0, i32 0) to i8*), i8* addrspacecast (i8 addrspace(1)* @__dso_handle to i8*))
A a;

// CHECK: call void @_ZN1BC1Ev(%struct.B* addrspacecast (%struct.B addrspace(1)* @b to %struct.B*))
// CHECK: call i32 @__cxa_atexit(void (i8*)* bitcast (void (%struct.B*)* @_ZN1BD1Ev to void (i8*)*), i8* addrspacecast (i8 addrspace(1)* getelementptr inbounds (%struct.B, %struct.B addrspace(1)* @b, i32 0, i32 0) to i8*), i8* addrspacecast (i8 addrspace(1)* @__dso_handle to i8*))
B b;

// PR6205: this should not require a global initializer
// CHECK-NOT: call void @_ZN1CC1Ev
C c;

// CHECK: call i32 @__cxa_atexit(void (i8*)* bitcast (void (%struct.D*)* @_ZN1DD1Ev to void (i8*)*), i8* addrspacecast (i8 addrspace(1)* getelementptr inbounds (%struct.D, %struct.D addrspace(1)* @d, i32 0, i32 0) to i8*), i8* addrspacecast (i8 addrspace(1)* @__dso_handle to i8*))
D d;

// <rdar://problem/7458115>
namespace test1 {
  int f();
  const int x = f();   // This has side-effects and gets emitted immediately.
  const int y = x - 1; // This gets deferred.
  const int z = ~y;    // This also gets deferred, but gets "undeferred" before y.
  int test() { return z; }
// CHECK-LABEL:      define i32 @_ZN5test14testEv()

  // All of these initializers end up delayed, so we check them later.
}

// <rdar://problem/8246444>
namespace test2 {
  struct allocator { allocator(); ~allocator(); };
  struct A { A(const allocator &a = allocator()); ~A(); };

  A a;
// CHECK: call void @_ZN5test29allocatorC1Ev(
// CHECK: invoke void @_ZN5test21AC1ERKNS_9allocatorE(
// CHECK: call void @_ZN5test29allocatorD1Ev(
// CHECK: call i32 @__cxa_atexit({{.*}} @_ZN5test21AD1Ev {{.*}} @_ZN5test21aE
}

namespace test3 {
  // Tested at the beginning of the file.
  const char * const var = "string";
  extern const char * const var;

  const char *test() { return var; }
}

namespace test4 {
  struct A {
    A();
  };
  extern int foo();

  // This needs an initialization function and guard variables.
  // CHECK: load i8, i8 addrspace(1)* bitcast (i64 addrspace(1)* @_ZGVN5test41xE to i8 addrspace(1)*)
  // CHECK: [[CALL:%.*]] = call i32 @_ZN5test43fooEv
  // CHECK-NEXT: store i32 %call, i32* addrspacecast (i32 addrspace(1)* @_ZN5test41xE to i32*)
  // CHECK-NEXT: store i64 1, i64 addrspace(1)* @_ZGVN5test41xE
  __attribute__((weak)) int x = foo();
}

namespace PR5974 {
  struct A { int a; };
  struct B { int b; };
  struct C : A, B { int c; };

  extern C c;

  // These should not require global initializers.
  A* a = &c;
  B* b = &c;
}

// PR9570: the indirect field shouldn't crash IR gen.
namespace test5 {
  static union {
    unsigned bar[4096] __attribute__((aligned(128)));
  };
}

namespace std { struct type_info; }

namespace test6 {
  struct A { virtual ~A(); };
  struct B : A {};
  extern A *p;

  // We must emit a dynamic initializer for 'q', because it could throw.
  B *const q = &dynamic_cast<B&>(*p);
  // CHECK: call void @__cxa_bad_cast()
  // CHECK: store {{.*}} @_ZN5test6L1qE

  // We don't need to emit 'r' at all, because it has internal linkage, is
  // unused, and its initialization has no side-effects.
  B *const r = dynamic_cast<B*>(p);
  // CHECK-NOT: call void @__cxa_bad_cast()
  // CHECK-NOT: store {{.*}} @_ZN5test6L1rE

  // This can throw, so we need to emit it.
  const std::type_info *const s = &typeid(*p);
  // CHECK: store {{.*}} @_ZN5test6L1sE

  // This can't throw, so we don't.
  const std::type_info *const t = &typeid(p);
  // CHECK-NOT: @_ZN5test6L1tE

  extern B *volatile v;
  // CHECK: store {{.*}} @_ZN5test6L1wE
  B *const w = dynamic_cast<B*>(v);

  // CHECK: load volatile
  // CHECK: store {{.*}} @_ZN5test6L1xE
  const int x = *(volatile int*)0x1234;

  namespace {
    int a = int();
    volatile int b = int();
    int c = a;
    int d = b;
    // CHECK-NOT: store {{.*}} @_ZN5test6{{[A-Za-z0-9_]*}}1aE
    // CHECK-NOT: store {{.*}} @_ZN5test6{{[A-Za-z0-9_]*}}1bE
    // CHECK-NOT: store {{.*}} @_ZN5test6{{[A-Za-z0-9_]*}}1cE
    // CHECK: load volatile {{.*}} @_ZN5test6{{[A-Za-z0-9_]*}}1bE
    // CHECK: store {{.*}} @_ZN5test6{{[A-Za-z0-9_]*}}1dE
  }
}

namespace test7 {
  struct A { A(); };
  struct B { ~B(); int n; };
  struct C { C() = default; C(const C&); int n; };
  struct D {};

  // CHECK: call void @_ZN5test71AC1Ev({{.*}}@_ZN5test7L1aE
  const A a = A();

  // CHECK: call i32 @__cxa_atexit({{.*}} @_ZN5test71BD1Ev{{.*}} @_ZN5test7L2b1E
  // CHECK: call i32 @__cxa_atexit({{.*}} @_ZN5test71BD1Ev{{.*}} @_ZGRN5test72b2E
  // CHECK: call void @_ZN5test71BD1Ev(
  // CHECK: store {{.*}} @_ZN5test7L2b3E
  const B b1 = B();
  const B &b2 = B();
  const int b3 = B().n;

  // CHECK-NOT: @_ZN5test7L2c1E
  // CHECK: call void @llvm.memset{{.*}} @_ZN5test7L2c1E
  // CHECK-NOT: @_ZN5test7L2c1E
  // CHECK: @_ZN5test7L2c2E
  // CHECK-NOT: @_ZN5test7L2c3E
  // CHECK: @_ZN5test7L2c4E
  const C c1 = C();
  const C c2 = static_cast<const C&>(C());
  const int c3 = C().n;
  const int c4 = C(C()).n;

  // CHECK-NOT: @_ZN5test7L1dE
  const D d = D();

  // CHECK: store {{.*}} @_ZN5test71eE
  int f(), e = f();
}


// At the end of the file, we check that y is initialized before z.

// CHECK:      define internal void [[TEST1_Z_INIT:@.*]]()
// CHECK:        load i32, i32* addrspacecast (i32 addrspace(4)* @_ZN5test1L1yE to i32*)
// CHECK-NEXT:   xor
// CHECK-NEXT:   store i32 {{.*}}, i32* addrspacecast (i32 addrspace(4)* @_ZN5test1L1zE to i32*)
// CHECK:      define internal void [[TEST1_Y_INIT:@.*]]()
// CHECK:        load i32, i32* addrspacecast (i32 addrspace(4)* @_ZN5test1L1xE to i32*)
// CHECK-NEXT:   sub
// CHECK-NEXT:   store i32 {{.*}}, i32* addrspacecast (i32 addrspace(4)* @_ZN5test1L1yE to i32*)

// CHECK: define internal void @_GLOBAL__sub_I_amdgcn_global_init.cpp() #{{[0-9]+}}
// CHECK:   call void [[TEST1_Y_INIT]]
// CHECK:   call void [[TEST1_Z_INIT]]

// rdar://problem/8090834: this should be nounwind
// CHECK-NOEXC: define internal void @_GLOBAL__sub_I_amdgcn_global_init.cpp() [[NUW:#[0-9]+]]

// CHECK-NOEXC: attributes [[NUW]] = { noinline nounwind{{.*}} }

// PR21811: attach the appropriate attribute to the global init function
// CHECK-FP: define internal void @_GLOBAL__sub_I_amdgcn_global_init.cpp() [[NUX:#[0-9]+]]
// CHECK-FP: attributes [[NUX]] = { noinline nounwind {{.*}}"no-frame-pointer-elim-non-leaf"{{.*}} }
