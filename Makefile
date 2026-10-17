
CXX = g++
CXXFLAGS = -std=c++17

tetris: tetris.cpp
	$(CXX) $(CXXFLAGS) tetris.cpp -o tetris

clean:
	rm -f tetris

