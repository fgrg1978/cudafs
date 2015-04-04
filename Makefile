TARGET = cudafs
SRCDIR = ./
FILES = 
INSTALLDIR= /usr/bin/

GCC = gcc 
GOP = -lpthread `pkg-config fuse --cflags --libs` -L./ -lcudafs 
NCC = nvcc
NOP = -Xcompiler -fPIC -shared

$(TARGET).o : $(TARGET).c
	$(NCC) cuda_utils.cu $(NOP) -o libcudafs.so
	$(GCC) $(TARGET).c $(GOP) -o $(TARGET)

clean:
	rm libcudafs.so
	rm $(TARGET)

install:
	cp $(TARGET) $(INSTALLDIR)

