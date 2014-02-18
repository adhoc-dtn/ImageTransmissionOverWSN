#include <deque>
#include <iostream>

using namespace std;

#define BUF 500000
// プロトタイプ宣言
void print(const deque<int>& deq);

deque<char> deq[BUF];  // int型の両端待ち行列
int main()
{
  // 要素を追加
  deq.push_back("hogehoge1" );   // 末尾に10
  deq.push_back("hogehoeg2" );   // 末尾に20
  
  print( deq );          // 結果出力
  
  // 要素を削除
  deq.pop_front();
  print( deq );          // 結果出力
  
  // 全削除
  deq.clear();
  print( deq );          // 結果出力
  
  return 0;
}

// dequeの要素を出力
void print(const deque<int>& deq)
{
	if( deq.empty() )
	{
		// dequeが空の場合
		cout << "dequeは空です";
	}
	else
	{
		// dequeが空ではない場合
		for(int i = 0; i < (int)deq.size(); ++i )
		{
			cout << deq[i] << ' ';
		}
	}
	cout << '\n' << endl;  // 改行
}
