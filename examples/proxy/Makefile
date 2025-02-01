# Get LINKING_TYPE (shared or static), GRPC_VER, GRPC_LIBS, etc.
include ../Makefile.conf    

# Target(s) to build
EXE_CLN = client
EXE_PRX = proxy
DEBUG = true

# Compiler and linker to use
CC = g++
LD = $(CC) 

# Compiler and linker to use
CC = g++
LD = $(CC)

# Configure Debug or Release build
CFLAGS = -std=gnu++17 -Wall -pthread
LDFLAGS = -pthread

ifeq "$(DEBUG)" "true"
  # Debug build
  CFLAGS += -g
else
  # Release build (-s to remove all symbol table and relocation info)
  CFLAGS += -O3 -DNDEBUG
  LDFLAGS += -s
endif

# Sources
PROJECT_HOME = .
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS_CLN = $(PROJECT_HOME)/client.cpp 
SRCS_PRX = $(PROJECT_HOME)/proxy.cpp 

# gRPC proto files 
PROTO_OUT  = $(OBJ_DIR)/_generate
PROTO_HOME = $(PROJECT_HOME)
PROTO_SRCS = $(PROTO_HOME)/hello.proto

GRPC_INC = $(GRPC_HOME)/$(LINKING_TYPE)/include
GRPC_BIN = $(GRPC_HOME)/$(LINKING_TYPE)/bin
GRPC_LIB = $(GRPC_HOME)/$(LINKING_TYPE)/lib
GRPC_LIB64 = $(GRPC_HOME)/$(LINKING_TYPE)/lib64

PROTOC = $(GRPC_BIN)/protoc
GRPC_CPP_PLUGIN = $(GRPC_BIN)/grpc_cpp_plugin

# Include directories
INCS = -I../../async_server \
       -I$(PROJECT_HOME) \
       -I$(GRPC_INC) \
       -I$(PROTO_OUT)

# Libraries
LIBS = -L$(GRPC_LIB) -L$(GRPC_LIB64)
ifeq "$(LINKING_TYPE)" "shared"
   # Shared linking.
   # Note: Using pkg-config only works for shared linking, it doesn't work for static
   export PKG_CONFIG_PATH=$(GRPC_LIB)/pkgconfig:$(GRPC_LIB64)/pkgconfig
   LIBS += `pkg-config --libs-only-l protobuf grpc++ grpc`

   # Note: Add libs requred by protoc to LD_LIBRARY_PATH
   export LD_LIBRARY_PATH += :$(GRPC_LIB):$(GRPC_LIB64)

   # Note: The order of precedence for *.so search path are: rpath, LD_LIBRARY_PATH, runpath.
   # Set option -Wl,--disable-new-dtags to tell the new linker to use the old behavior, i.e. RPATH.
   # Set option -Wl,--enable-new-dtags to tell the old linker to use the new behavior, i.e. RUNPATH.
   # To verify: readelf -d <exefile> | grep PATH
   LDFLAGS += -Wl,--disable-new-dtags
   LDFLAGS += -Wl,-rpath='$$ORIGIN/$(GRPC_LIB)' -Wl,-rpath='$$ORIGIN/$(GRPC_LIB64)'
else
   # Static linking.
   # Note: GRPC_LIBS is defined in Makefile.conf
   LIBS += $(GRPC_LIBS)
endif

#ifeq "$(OS)" "Darwin"
#  LIBS += -framework CoreFoundation
#endif

#  For gRpc reflection, use --no-as-needed flag for dynamic linking:
#       -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed
# or you might need to use --whole-archive
#       -Wl,--whole-archive -lgrpc++_reflection -Wl,--no-whole-archive

# gRpc files to generate from *.proto files 
PROTO_NAMES = $(basename $(notdir $(PROTO_SRCS)))

PROTOC_CC   = $(addprefix $(PROTO_OUT)/, $(addsuffix .pb.cc, $(PROTO_NAMES)))
PROTOC_OBJS = $(addprefix $(OBJ_DIR)/,   $(addsuffix .pb.o,  $(PROTO_NAMES)))

GRPC_CC     = $(addprefix $(PROTO_OUT)/, $(addsuffix .grpc.pb.cc, $(PROTO_NAMES)))
GRPC_OBJS   = $(addprefix $(OBJ_DIR)/,   $(addsuffix .grpc.pb.o,  $(PROTO_NAMES)))

# Objective files to build
OBJS_CLN =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_CLN)))))
OBJS_CLN += $(PROTOC_OBJS) $(GRPC_OBJS)

OBJS_PRX =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_PRX)))))
OBJS_PRX += $(PROTOC_OBJS) $(GRPC_OBJS)


# Build target(s)
all: $(EXE_CLN) $(EXE_PRX)

$(EXE_CLN): $(PROTOC_CC) $(GRPC_CC) $(OBJS_CLN) 
	$(LD) $(LDFLAGS) -o $(EXE_CLN) $(OBJS_CLN) $(LIBS)
	
$(EXE_PRX): $(PROTOC_CC) $(GRPC_CC) $(OBJS_PRX) 
	$(LD) $(LDFLAGS) -o $(EXE_PRX) $(OBJS_PRX) $(LIBS)	

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(PROJECT_HOME)/%.cpp Makefile   
	-mkdir -p $(OBJ_DIR)
	$(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Compile gRpc source files 
$(OBJ_DIR)/%.o: $(PROTO_OUT)/%.cc Makefile
	-mkdir -p $(OBJ_DIR)
	$(CC) -c $(CFLAGS) $(INCS) -I$(PROTO_OUT) -o $(OBJ_DIR)/$*.o $<

# Generate gRpc files
$(PROTO_OUT)/%.grpc.pb.cc: $(PROTO_HOME)/%.proto Makefile
	@echo ">>> Generating grpc files from $<..."
	-mkdir -p $(PROTO_OUT)
	$(PROTOC) --grpc_out=$(PROTO_OUT) -I $(PROTO_HOME) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

# Generate protobuf files
$(PROTO_OUT)/%.pb.cc: $(PROTO_HOME)/%.proto Makefile
	@echo ">>> Generating proto files from $<..."
	-mkdir -p $(PROTO_OUT)
	$(PROTOC) --cpp_out=$(PROTO_OUT) --proto_path=$(PROTO_HOME) $<

# Delete all intermediate files
clean clear:
	rm -rf $(EXE_CLN) $(EXE_PRX) $(OBJ_DIR)

# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
-include $(OBJS_CLN:.o=.d)
-include $(OBJS_PRX:.o=.d)


