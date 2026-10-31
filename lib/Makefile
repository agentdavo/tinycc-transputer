#
# Tiny C Compiler Makefile for libtcc1.a
#

TOP = ..
include $(TOP)/Makefile
VPATH = $(TOPSRC)/lib $(TOPSRC)/win32/lib
T = $(or $(CROSS_TARGET),$(NATIVE_TARGET),unknown)
X = $(if $(CROSS_TARGET),$(CROSS_TARGET)-)
XCFG = $(or $(findstring -win,$T),-unx)
S = $(if $(findstring yes,$(SILENT)),@$(info * $@))

TCC = $(TOP)/$(X)tcc$(EXESUF)
XTCC ?= $(TOP)/$(X)tcc$(EXESUF)
XCC = $(XTCC)
XAR = $(XTCC) -ar
XFLAGS-unx = -B$(TOPSRC)
XFLAGS-win = -B$(TOPSRC)/win32 -I$(TOPSRC)/include
XFLAGS = $(XFLAGS$(XCFG))
BFLAGS = -bt

# in order to use gcc, type: make <target>-libtcc1-usegcc=yes
arm-libtcc1-usegcc ?= no

# This makes bounds checking 40%..60% faster.
#x86_64-libtcc1-usegcc=yes
#i386-libtcc1-usegcc=yes

ifeq "$($(T)-libtcc1-usegcc)" "yes"
 XCC = $(CC)
 XAR = $(AR)
 XFLAGS = $(CFLAGS) -fPIC -fno-omit-frame-pointer -Wno-unused-function -Wno-unused-variable
 BFLAGS = $(if $(CONFIG_dwarf),-gdwarf,-gstabs)
endif

XFLAGS += -I$(TOP)

I386_O = libtcc1.o $(COMMON_O)
X86_64_O = libtcc1.o $(COMMON_O)
ARM_O = libtcc1.o armeabi.o armflush.o $(COMMON_O)
ARM64_O = lib-arm64.o $(COMMON_O)
RISCV64_O = lib-arm64.o $(COMMON_O)
COMMON_O = stdatomic.o atomic.o builtin.o alloca.o alloca-bt.o
WIN_O = crt1.o crt1w.o wincrt1.o wincrt1w.o dllcrt1.o dllmain.o winex.o
LIN_O = dsohandle.o
OSX_O =

# backtrace/bcheck/run only for native compiler
Nat = $(if $X,no,)
Cbt = $(Nat)$(subst yes,,$(CONFIG_backtrace))
Cbc = $(Cbt)$(subst yes,,$(CONFIG_bcheck))

$(Nat)COMMON_O += runmain.o tcov.o
$(Cbt)COMMON_O += bt-exe.o bt-log.o
$(Cbt)WIN_O += bt-dll.o
$(Cbc)COMMON_O += bcheck.o bcheck_run.o

# not in libtcc1.a
EXTRA_O = runmain.o bt-exe.o bt-dll.o bt-log.o bcheck.o bcheck_run.o

OBJ-i386 = $(I386_O) pic86.o $(LIN_O)
OBJ-x86_64 = $(X86_64_O) va_list.o $(LIN_O)
OBJ-x86_64-osx = $(X86_64_O) va_list.o $(OSX_O)
OBJ-i386-win32 = $(I386_O) chkstk.o $(WIN_O)
OBJ-x86_64-win32 = $(X86_64_O) chkstk.o $(WIN_O)
OBJ-arm64 = $(ARM64_O) armflush.o $(LIN_O)
OBJ-arm64-osx = $(ARM64_O) $(OSX_O)
OBJ-arm64-win32 = $(ARM64_O) chkstk.o $(WIN_O)
OBJ-arm = $(ARM_O) $(LIN_O)
OBJ-arm-fpa = $(OBJ-arm)
OBJ-arm-fpa-ld = $(OBJ-arm)
OBJ-arm-vfp = $(OBJ-arm)
OBJ-arm-eabi = $(OBJ-arm)
OBJ-arm-eabihf = $(OBJ-arm)
OBJ-arm-wince = $(ARM_O) $(WIN_O)
OBJ-riscv64 = $(RISCV64_O) armflush.o $(LIN_O)
OBJ-transputer = libtcc1.o transputer-runtime.o transputer-conc.o transputer-f16.o transputer-vla.o \
                 $(TVX-transputer) $(COMMON_O)
# Software TVX (transputer-tvx*.c): the TVX instructions as C, for parts
# without TVX.
TVX-transputer = transputer-tvx.o transputer-tvx-iq.o transputer-tvx-qdot.o

OBJ-extra = $(filter $(EXTRA_O),$(OBJ-$T))
OBJ-libtcc1 = $(addprefix $(X),$(filter-out $(OBJ-extra),$(OBJ-$T)))

ALL = $(addprefix $(TOP)/,$(X)libtcc1.a $(OBJ-extra))

all: $(ALL)

$(TOP)/$(X)libtcc1.a : $(OBJ-libtcc1) $(TCC)
	$S$(XAR) rcs $@ $(OBJ-libtcc1)

# The same runtime again for the FPU-less IMS T425 (-mt425) and T414
# (-mt414), each with the soft-float helpers its floating point calls, and
# for the T800 (-mt800 and -mt805): the default one is -mt9000 code.
ifeq "$T" "transputer"
SOFTFP-transputer = transputer-softfp.o transputer-softfp-sn.o transputer-softfp-db.o
OBJ-t425 = $(patsubst %.o,transputer-t425-%.o,$(OBJ-transputer) $(SOFTFP-transputer))
OBJ-t414 = $(patsubst %.o,transputer-t414-%.o,$(OBJ-transputer) $(SOFTFP-transputer))
OBJ-t800 = $(patsubst %.o,transputer-t800-%.o,$(OBJ-transputer))

all: $(TOP)/transputer-libtcc1-t425.a $(TOP)/transputer-libtcc1-t414.a
all: $(TOP)/transputer-libtcc1-t800.a

# Software TVX is built from t4's semantic headers (vendored in
# transputer-tvx/), which include <stdint.h> and friends: give it a
# libc-free set rather than the host's /usr/include.
$(addprefix $(X),$(TVX-transputer)) \
$(addprefix transputer-t800-,$(TVX-transputer)) \
$(addprefix transputer-t425-,$(TVX-transputer)) \
$(addprefix transputer-t414-,$(TVX-transputer)) : \
    XFLAGS += -nostdinc -I$(TOPSRC)/lib/transputer-tvx/include -I$(TOPSRC)/include

$(TOP)/transputer-libtcc1-t425.a : $(OBJ-t425) $(TCC)
	$S$(XAR) rcs $@ $(OBJ-t425)

$(TOP)/transputer-libtcc1-t414.a : $(OBJ-t414) $(TCC)
	$S$(XAR) rcs $@ $(OBJ-t414)

$(TOP)/transputer-libtcc1-t800.a : $(OBJ-t800) $(TCC)
	$S$(XAR) rcs $@ $(OBJ-t800)

transputer-t800-%.o : %.c $(TCC)
	$S$(XCC) -mt800 -c $< -o $@ $(XFLAGS)

transputer-t800-%.o : %.S $(TCC)
	$S$(XCC) -mt800 -c $< -o $@ $(XFLAGS)

transputer-t425-%.o : %.c $(TCC)
	$S$(XCC) -mt425 -c $< -o $@ $(XFLAGS)

transputer-t425-%.o : %.S $(TCC)
	$S$(XCC) -mt425 -c $< -o $@ $(XFLAGS)

transputer-t414-%.o : %.c $(TCC)
	$S$(XCC) -mt414 -c $< -o $@ $(XFLAGS)

transputer-t414-%.o : %.S $(TCC)
	$S$(XCC) -mt414 -c $< -o $@ $(XFLAGS)
endif

$(X)%.o : %.c $(TCC)
	$S$(XCC) -c $< -o $@ $(XFLAGS)

$(X)%.o : %.S $(TCC)
	$S$(XCC) -c $< -o $@ $(XFLAGS)

$(TOP)/%.o : %.c $(TCC)
	$S$(XCC) -c $< -o $@ $(XFLAGS)

$(TOP)/bcheck.o : XFLAGS += $(BFLAGS)
$(TOP)/bcheck_run.o : bcheck.c $(TCC)
	$S$(XCC) -c bcheck.c -o $@ $(XFLAGS)  $(BFLAGS) -DBCHECK_RUN

$(X)crt1w.o : crt1.c
$(X)wincrt1w.o : wincrt1.c

# don't try to make it
$(TCC) : ;

clean :
	rm -f *.o $(addprefix $(TOP)/,*libtcc1.a $(EXTRA_O))
