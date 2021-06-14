
# Target(s) to build
TARGET_SRV = server
TARGET_CLN = client

# Sources
PROJECT_HOME = .
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS_SRV = $(PROJECT_HOME)/grpcServer.cpp \
           $(PROJECT_HOME)/main.cpp \
           $(PROJECT_HOME)/testServer.cpp \
           $(PROJECT_HOME)/testService.cpp 

SRCS_CLN = $(PROJECT_HOME)/client.cpp 

# gRPC proto files 
PROTO_HOME = $(PROJECT_HOME)
PROTO_SRCS = $(PROTO_HOME)/test.proto 
PROTO_OUT  = $(PROJECT_HOME)/_generate

# Detect operating system
OS = $(shell uname -s)

# gRPC support
ifeq "$(OS)" "Linux"
  GRPC_HOME = ../grpc_1.11.0_install/linux
else
  GRPC_HOME = ../grpc_1.11.0_install/mac
endif

GRPC_INC = $(GRPC_HOME)/include
GRPC_BIN = $(GRPC_HOME)/bin
GRPC_LIB = $(GRPC_HOME)/lib

PROTOC          = $(GRPC_BIN)/protoc
GRPC_CPP_PLUGIN = $(GRPC_BIN)/grpc_cpp_plugin

# Include directories
INCS = -I$(PROJECT_HOME) \
       -I$(PROJECT_HOME)\gen \
       -I$(GRPC_INC) \
       -I$(PROTO_OUT)


# Libraries
LIBS = -lpthread \
       -L$(GRPC_LIB) \
       -lgrpc -lgrpc++ -lgrpc++_reflection \
       -lgrpc_unsecure -lgrpc++_unsecure \
       -lprotobuf -lcrypto -lssl \
       -lgpr -lz -laddress_sorting -lcares

# For gRpc reflection, use --no-as-needed flag for dynamic linking:
#       -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed
# or you might need to use --whole-archive
#       -Wl,--whole-archive -lgrpc++_reflection -Wl,--no-whole-archive

# gRpc files to generate from *.proto files 
PROTO_NAMES   = $(basename $(notdir $(PROTO_SRCS)))

PROTOC_CC   = $(addprefix $(PROTO_OUT)/, $(addsuffix .pb.cc, $(PROTO_NAMES)))
PROTOC_OBJS = $(addprefix $(OBJ_DIR)/,   $(addsuffix .pb.o,  $(PROTO_NAMES)))

GRPC_CC     = $(addprefix $(PROTO_OUT)/, $(addsuffix .grpc.pb.cc, $(PROTO_NAMES)))
GRPC_OBJS   = $(addprefix $(OBJ_DIR)/,   $(addsuffix .grpc.pb.o,  $(PROTO_NAMES)))

# Objective files to build
OBJS_SRV =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_SRV)))))
OBJS_SRV += $(PROTOC_OBJS) $(GRPC_OBJS)

OBJS_CLN =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_CLN)))))
OBJS_CLN += $(PROTOC_OBJS) $(GRPC_OBJS)

# Compiler and linker to use
CC = g++
CFLAGS = -std=c++11 -Wall -pthread
LD = $(CC)

# Build target(s)
all: $(TARGET_SRV) $(TARGET_CLN)

$(TARGET_SRV): $(PROTOC_CC) $(GRPC_CC) $(OBJS_SRV) 
	$(LD) $(LDFLAGS) -o $(TARGET_SRV) $(OBJS_SRV) $(LIBS)

$(TARGET_CLN): $(PROTOC_CC) $(GRPC_CC) $(OBJS_CLN) 
	$(LD) $(LDFLAGS) -o $(TARGET_CLN) $(OBJS_CLN) $(LIBS)

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
	@echo ">>> Generating grpc files..."
	-mkdir -p $(PROTO_OUT)
	$(PROTOC) --grpc_out=$(PROTO_OUT) -I $(PROTO_HOME) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

# Generate protobuf files
$(PROTO_OUT)/%.pb.cc: $(PROTO_HOME)/%.proto Makefile
	@echo ">>> Generating proto files..."
	-mkdir -p $(PROTO_OUT)
	$(PROTOC) --cpp_out=$(PROTO_OUT) --proto_path=$(PROTO_HOME) $<

# Delete all intermediate files
clean: 
#	@echo PROTO_SRCS = $(PROTO_SRCS)
#	@echo PROTO_NAMES = $(PROTO_NAMES)
#	@echo PROTO_OUT = $(PROTO_OUT)
#	@echo PROTOC_OBJS = $(PROTOC_OBJS)
#	@echo GRPC_OBJS = $(GRPC_OBJS)
#	@echo OBJS_SRV = $(OBJS_SRV)
#	@echo OBJS_CLN = $(OBJS_CLN)
	rm -rf $(TARGET_SRV) $(TARGET_CLN) $(OBJ_DIR) $(PROTO_OUT) core

#
# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
#
-include $(OBJS_SRV:.o=.d)
-include $(OBJS_CLN:.o=.d)


