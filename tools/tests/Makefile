ROOT := ../..
CXXFLAGS := -std=gnu++17 -Wall -Wextra -Werror -I$(ROOT)/src

test: versionCompare_test
	./versionCompare_test

versionCompare_test: versionCompare_test.cpp $(ROOT)/src/versionCompare.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f versionCompare_test

.PHONY: test clean
