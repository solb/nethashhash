#include <unordered_map>
#include <cstdio>

using std::unordered_map;

int main() {
	unordered_map<const char*, int> map;

	map["foo"] = 3;

	printf("%d\n", map["foo"]);
}
