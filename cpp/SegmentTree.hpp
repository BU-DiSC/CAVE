
#include <vector>
struct SegTreeNode {
  int val = 0;
  int max_val = 0;
  int val_2;
};

class SegmentTree {
private:
  int length;
  std::vector<SegTreeNode> nodes;
  void init_tree();
  void init_tree(int val);
  void _update(int id, int l, int r, int pos, int val);
  void maintain(int id);

public:
  SegmentTree(int len, int val);
  int query_first_larger(int val);
  void update(int pos, int val);
  void update_id(int id, int val, int val2);
  int get_val(int id);
  int get_val2(int id);
};
