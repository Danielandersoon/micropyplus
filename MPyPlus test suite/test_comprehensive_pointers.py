#!/usr/bin/env micropython
import gc
print("----- COMPREHENSIVE POINTER TEST SUITE -----\n")

pass_count = 0
fail_count = 0
arr = [100, 200, 300, 400, 500]


def test(desc):
    def decorator(func):
        global pass_count, fail_count
        try:
            result = func()
            if result:
                print("pass:clear " + desc)
                pass_count += 1
            else:
                print("fail: " + desc)
                fail_count += 1
        except Exception as e:
            print("fail: " + desc + " (Exception: " + str(e) + ")")
            fail_count += 1
        gc.collect()  # Collect garbage after each test to prevent memory buildup
        return func
    return decorator

@test("Create and dereference integer pointer")
def test_int_pointer():
    x = 42
    px = &x
    return *px == 42

@test("Dereference negative integer")
def test_neg_int():
    y = -100
    py = &y
    return *py == -100

@test("String pointer")
def test_str():
    s = "hello"
    ps = &s
    return *ps == "hello"

@test("String pointer sees update")
def test_str_update():
    s = "hello"
    ps = &s
    s = "world"
    return *ps == "world"

@test("Boolean pointer")
def test_bool():
    t = True
    pt = &t
    return *pt == True

@test("None pointer")
def test_none():
    n = None
    pn = &n
    return *pn is None

@test("Float pointer")
def test_float():
    f = 3.14
    pf = &f
    return *pf == 3.14

@test("List pointer")
def test_list():
    lst = [1, 2, 3]
    plst = &lst
    return *plst == lst

@test("Dict pointer")
def test_dict():
    d = {'a': 1, 'b': 2}
    pd = &d
    return *pd == d

@test("Object pointer deref")
def test_obj():
    class Point:
        def __init__(self, x):
            self.x = x
    p = Point(5)
    pp = &p
    return (*pp).x == 5

@test("Arrow operator member read")
def test_arrow_read():
    class Point:
        def __init__(self, x):
            self.x = x
    p = Point(10)
    pp = &p
    return pp->x == 10

@test("Arrow operator method call")
def test_arrow_method():
    class Obj:
        def __init__(self, val):
            self.val = val
        def get_val(self):
            return self.val
    o = Obj(99)
    po = &o
    return po->get_val() == 99

@test("Double pointer dereference")
def test_double_ptr():
    x = 777
    px = &x
    ppx = &px
    px_deref = *ppx
    return *px_deref == 777

@test("Global pointer")
def test_global():
    global g_val
    g_val = 555
    pgv = &g_val
    return *pgv == 555

@test("Global modification visible")
def test_global_update():
    global g_val
    g_val = 555
    pgv = &g_val
    g_val = 666
    return *pgv == 666

@test("Multiple pointers same var")
def test_multi_ptr():
    val = 123
    p1 = &val
    p2 = &val
    p3 = &val
    v1 = *p1
    v2 = *p2
    v3 = *p3
    return v1 == 123 and v2 == 123 and v3 == 123

@test("Loop with pointer")
def test_loop():
    count = 0
    pcount = &count
    for i in range(5):
        count = count + 1
    return *pcount == 5

@test("Conditional with pointer")
def test_cond():
    val = 50
    pval = &val
    deref_val = *pval
    return deref_val > 40

@test("Pointer to pointer to pointer")
def test_triple_ptr():
    x = 888
    px = &x
    ppx = &px
    pppx = &ppx
    return *(*(*pppx)) == 888

@test("Pointer arithmetic")
def test_pointer_arithmetic():
    x = 10
    y = 50
    px = &x
    py = &y
    diff = py - px
    return *(px + diff) == 50

@test("Pointer subscript basic element access")
def test_pointer_subscript():
    test_arr = [42]
    test_ptr = &test_arr[0]
    # Store dereference result before comparison
    deref_result = *test_ptr
    return deref_result == 42

@test("Pointer subscript with positive offset")
def test_ptr_subscript_offset():
    arr = [100, 200, 300, 400, 500]
    ptr = &arr[0]
    ptr2 = ptr + 24  # 3 elements * 8 bytes each
    return *ptr2 == 400

@test("Pointer subscript with negative offset")
def test_ptr_subscript_negative():
    arr = [100, 200, 300, 400, 500]
    ptr = &arr[2]
    return *(ptr - 16) == 100
    
# Print summary
print("\n" + "-"*50)
print("TOTAL TESTS: " + str(pass_count + fail_count))
print("PASSED: " + str(pass_count))
print("FAILED: " + str(fail_count))
print("-"*50)
if fail_count > 0:
    import sys
    sys.exit(1)
else:
    print("\nALL TESTS PASSED!!!!!!!")
