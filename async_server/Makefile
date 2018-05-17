
# Target(s) to build
TARGET = server
TARGET2 = client

# Sources
PROJECT_HOME = .
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS = $(PROJECT_HOME)/main.cpp \
       $(PROJECT_HOME)/grpcServer.cpp \
       $(PROJECT_HOME)/grpcServerImpl.cpp 

SRCS2 = $(PROJECT_HOME)/client.cpp 

# gRPC proto files 
PROTO_HOME = $(PROJECT_HOME)
PROTO_SRCS = $(PROTO_HOME)/grpcServer.proto 
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
       -I$(GRPC_INC) \
       -I$(PROTO_OUT)

# Libraries
LIBS = -lpthread \
       -L$(GRPC_LIB) \
       -lprotobuf -lcrypto -lssl -lgrpc -lgrpc++ \
       -lgrpc++_reflection -lgrpc_unsecure -lgrpc++_unsecure -lgpr -lz \
       -laddress_sorting -lcares

# gRpc files to generate from *.proto files 
PROTO_NAMES   = $(basename $(notdir $(PROTO_SRCS)))

PROTOC_CC     = $(addprefix $(PROTO_OUT)/, $(addsuffix .pb.cc, $(PROTO_NAMES)))
PROTOC_CC_OBJ = $(addprefix $(OBJ_DIR)/,   $(addsuffix .pb.o,  $(PROTO_NAMES)))

GRPC_CC       = $(addprefix $(PROTO_OUT)/, $(addsuffix .grpc.pb.cc, $(PROTO_NAMES)))
GRPC_CC_OBJ   = $(addprefix $(OBJ_DIR)/,   $(addsuffix .grpc.pb.o,  $(PROTO_NAMES)))

# Objective files to build
OBJS = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS)))))
OBJS += $(PROTOC_CC_OBJ) $(GRPC_CC_OBJ)

OBJS2 = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS2)))))
OBJS2 += $(PROTOC_CC_OBJ) $(GRPC_CC_OBJ)

# Compiler and linker to use
CC = g++
CFLAGS = -std=c++11 -Wall -pthread
LD = $(CC)

# Build target(s)
all: $(TARGET) $(TARGET2)

$(TARGET): $(PROTOC_CC) $(GRPC_CC) $(OBJS) 
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

$(TARGET2): $(PROTOC_CC) $(GRPC_CC) $(OBJS2) 
	$(LD) $(LDFLAGS) -o $(TARGET2) $(OBJS2) $(LIBS)

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
#	@echo PROTOC_CC_OBJ = $(PROTOC_CC_OBJ)
#	@echo GRPC_CC_OBJ = $(GRPC_CC_OBJ)
#	@echo OBJS = $(OBJS)
#	@echo OBJS2 = $(OBJS2)
	rm -rf $(TARGET) $(TARGET2) $(OBJ_DIR) $(PROTO_OUT) core

#
# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
#
-include $(OBJS:.o=.d)
-include $(OBJS2:.o=.d)


