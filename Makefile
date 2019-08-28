CC = gcc
AR = ar
CXX = g++

CFLAGS := $$(pkg-config --libs --cflags libcurl json) -fPIC -Wall -Wextra -O3 -g -static
CFLAGS += -D_DEBUG_
LDFLAGS := $(shell pkg-config --libs --cflags libcurl json --static opencv)
LDFLAGS += -lpthread

OBJS = faceapi.o
LIB = libFaceAPI.a
LIB_NAME = libFaceAPI
SAMPLE_EXC = innofaceguard
SAMPLE_SRC = innofaceguard.cpp
SAMPLE_CFLAGS := -DLOG_ENABLE -DLOGFILE_ENABLE
SAMPLE_CFLAGS := -Wall $(shell pkg-config --cflags opencv)
LIB_PATH = ./
INCLUDE_PATH = -Iinclude/
OUT_PATH = ./bin

all : $(LIB) $(SAMPLE_EXC)

%.o : %.c
	$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@ 

$(LIB) : $(OBJS)
	rm -f $@
	$(AR) -crs $@ $(OBJS)
	rm -f $(OBJS)

$(SAMPLE_EXC):
	$(CXX) $(SAMPLE_CFLAGS) $(SAMPLE_SRC) $(LIB) -o $(OUT_PATH)/$(SAMPLE_EXC) $(LDFLAGS) 

clean:
	rm -f $(OBJS) $(TARGET) $(LIB) $(OUT_PATH)/$(SAMPLE_EXC) $(OUT_PATH)/*.jpg
