#include <iostream>
using namespace std;
class test {
public:
	int _day = 1;
	int _month = 1;
	int _year = 1;
private:
	int num = 1;
};

int main() {
	test t1;
	t1._day = 1;
	cout << t1._day << endl;
}
