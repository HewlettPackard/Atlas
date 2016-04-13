CXXFLAGS += -g -O0 -coverage

helloworld: helloworld.cc

#	gcc -g -O0 –coverage -o helloworld helloworld.cc -I. -liostream

.PHONY: test
test: helloworld
	./helloworld