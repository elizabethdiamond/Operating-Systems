override CFLAGS := -Wall -Werror -std=gnu99 -O0 -g $(CFLAGS) -I.

# Build the fs.o file
fs.o: fs.c fs.h disk.h

# Automatically discover all test files
test_c_files=$(shell find tests -type f -name '*.c')
test_o_files=$(test_c_files:.c=.o)
test_files=$(test_c_files:.c=)

# The intermediate test .o files shouldn't be auto-deleted in test runs; they
# may be useful for incremental builds while fixing fs.c bugs.
.SECONDARY: $(test_o_files)

.PHONY: clean check checkprogs
        
# Rules to build each individual test
tests/%: tests/%.o fs.o
		$(CC) $(LDFLAGS) $+ $(LOADLIBES) $(LDLIBS) -o $@

# Build all of the test programs
checkprogs: $(test_files)

# Run the test programs
check: checkprogs
		tests/run_tests.sh $(test_files)

clean:
	rm -f *.o $(test_files) $(test_o_files)

