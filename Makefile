# Simple Makefile for SeedApp

TARGET = seedapp
SOURCE = SeedApp.cpp

# Build the program
$(TARGET): $(SOURCE)
	g++ -o $(TARGET) $(SOURCE) -pthread

# Clean build files
clean:
	rm -f $(TARGET)

# Build and run
run: $(TARGET)
	./$(TARGET)

.PHONY: clean run
