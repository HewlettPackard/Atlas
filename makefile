helloworld: helloworld.cc
	gcc -g -O0 –coverage -o helloworld helloworld.cc -I. -liostream

test: helloworld