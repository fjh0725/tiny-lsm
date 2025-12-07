#include "../../include/skiplist/skiplist.h"
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace tiny_lsm {

// ************************ SkipListIterator ************************
BaseIterator &SkipListIterator::operator++() {
  // TODO: Lab1.2 任务：实现SkipListIterator的++操作符
  this->current = this->current->forward_[0];
  return *this;
}

bool SkipListIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的==操作符
  if(other.get_type() != IteratorType::SkipListIterator) {
    return false;
  }
  return this->current.get() == static_cast<const SkipListIterator &>(other).current.get();
}

bool SkipListIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的!=操作符
  if(other.get_type() != IteratorType::SkipListIterator) {
    return true;
  }
  return this->current.get() != static_cast<const SkipListIterator &>(other).current.get();
}

SkipListIterator::value_type SkipListIterator::operator*() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的*操作符
  return {current->key_, current->value_};
}

IteratorType SkipListIterator::get_type() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的get_type
  // ? 主要是为了熟悉基类的定义和继承关系
  return IteratorType::SkipListIterator;
}

bool SkipListIterator::is_valid() const {
  return current && !current->key_.empty();
}
bool SkipListIterator::is_end() const { return current == nullptr; }

std::string SkipListIterator::get_key() const { return current->key_; }
std::string SkipListIterator::get_value() const { return current->value_; }
uint64_t SkipListIterator::get_tranc_id() const { return current->tranc_id_; }

// ************************ SkipList ************************
// 构造函数
SkipList::SkipList(int max_lvl) : max_level(max_lvl), current_level(1) {
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  dis_01 = std::uniform_int_distribution<>(0, 1);
  dis_level = std::uniform_int_distribution<>(0, (1 << max_lvl) - 1);
  gen = std::mt19937(std::random_device()());
}

int SkipList::random_level() {
  // ? 通过"抛硬币"的方式随机生成层数：
  // ? - 每次有50%的概率增加一层
  // ? - 确保层数分布为：第1层100%，第2层50%，第3层25%，以此类推
  // ? - 层数范围限制在[1, max_level]之间，避免浪费内存
  // TODO: Lab1.1 任务：插入时随机为这一次操作确定其最高连接的链表层数
  int res  = 1;
  unsigned int tmp = dis_level(gen);
  while ((tmp & 1) == 1 && res < max_level) {
    res++;
    tmp >>= 1;
  }
  return res;
}

void SkipList::FindLessThan(std::vector<std::shared_ptr<SkipListNode>> &prev, std::shared_ptr<SkipListNode> target) {
  auto cur = head;
  for (int level = current_level - 1; level >= 0; level --) {
    while(cur->forward_[level] != nullptr && *cur->forward_[level] < *target) {
      cur = cur->forward_[level];
    }
    prev[level] = cur;
  }
}

void SkipList::FindLessThan(std::vector<std::shared_ptr<SkipListNode>> &prev, const std::string &target){
  auto cur = head;
  for (int level = current_level - 1; level >= 0; level --) {
    while(cur->forward_[level] != nullptr && cur->forward_[level]->key_ < target) {
      cur = cur->forward_[level];
    }
    prev[level] = cur;
  }
}

// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {})", key, value, tranc_id);
  
  // TODO: Lab1.1  任务：实现插入或更新键值对
  // ? Hint: 你需要保证不同`Level`的步长从底层到高层逐渐增加
  // ? 你可能需要使用到`random_level`函数以确定层数, 其注释中为你提供一种思路
  // ? tranc_id 为事务id, 现在你不需要关注它, 直接将其传递到 SkipListNode 的构造函数中即可
  int genLevel = random_level();
  auto tmp = std::make_shared<SkipListNode>(key, value, genLevel, tranc_id);
  auto prevs = std::vector<std::shared_ptr<SkipListNode>>(max_level, head);
  FindLessThan(prevs, tmp);
  current_level = std::max(current_level, genLevel);
  if (prevs[0]->forward_[0] != nullptr && prevs[0]->forward_[0]->key_ == key && prevs[0]->forward_[0]->tranc_id_ == tranc_id) {
    prevs[0]->forward_[0]->value_ = value;
    size_bytes += value.size() - prevs[0]->forward_[0]->value_.size();
    return;
  } else {
    size_bytes += key.size() + value.size() + sizeof(uint64_t);
  }
  
  for (int i = genLevel - 1; i >= 0; i --) {
    tmp->forward_[i] = prevs[i]->forward_[i];
    if (prevs[i]->forward_[i] != nullptr)
      prevs[i]->forward_[i]->set_backward(i, tmp);
    prevs[i]->forward_[i] = tmp;
    tmp->set_backward(i, prevs[i]);
  }
}

// 查找键值对
SkipListIterator SkipList::get(const std::string &key, uint64_t tranc_id) {
  // spdlog::trace("SkipList--get({}) called", key);
  // ? 你可以参照上面的注释完成日志输出以便于调试
  // ? 日志为输出到你执行二进制所在目录下的log文件夹
  // TODO: Lab1.1 任务：实现查找键值对,
  auto tmp = std::make_shared<SkipListNode>(key, "", 0, tranc_id);
  auto prevs = std::vector<std::shared_ptr<SkipListNode>>(max_level, head);
  FindLessThan(prevs, tmp);

  auto curr = prevs[0]->forward_[0];
  if(curr != nullptr && curr->key_ == key && curr->tranc_id_ == tranc_id) {
    // std::cout << "key1: " << curr->key_ << ", tranc_id: " << tranc_id << std::endl;
    return SkipListIterator(prevs[0]->forward_[0]);
  }
  // TODO: 并且你后续需要额外实现SkipListIterator中的TODO部分(Lab1.2)
  return SkipListIterator{};
}

// 删除键值对
// ! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除,
// ! 这里只是为了实现完整的 SkipList 不会真正被上层调用
void SkipList::remove(const std::string &key) {
  // TODO: Lab1.1 任务：实现删除键值对
  auto prevs = std::vector<std::shared_ptr<SkipListNode>>(max_level, head);
  FindLessThan(prevs, key);
  if(prevs[0]->forward_[0] == nullptr || prevs[0]->forward_[0]->key_ != key) {
    return;
  }
  size_bytes -= prevs[0]->forward_[0]->key_.size() + prevs[0]->forward_[0]->value_.size() + sizeof(uint64_t);
  auto curr = prevs[0]->forward_[0];
  for (int i = 0; i < current_level; i ++) {
    if (prevs[i]->forward_[i] != curr) {
      break;
    }
    prevs[i]->forward_[i] = curr->forward_[i];
    if (curr->forward_[i] != nullptr)
      curr->forward_[i]->set_backward(i, prevs[i]);
  }
  while(current_level > 1 && head->forward_[current_level - 1] == nullptr) {
    current_level --;
  }
}

// 刷盘时可以直接遍历最底层链表
std::vector<std::tuple<std::string, std::string, uint64_t>> SkipList::flush() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  spdlog::debug("SkipList--flush(): Starting to flush skiplist data");

  std::vector<std::tuple<std::string, std::string, uint64_t>> data;
  auto node = head->forward_[0];
  while (node) {
    data.emplace_back(node->key_, node->value_, node->tranc_id_);
    node = node->forward_[0];
  }

  spdlog::debug("SkipList--flush(): Flushed {} entries", data.size());

  return data;
}

size_t SkipList::get_size() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  return size_bytes;
}

// 清空跳表，释放内存
void SkipList::clear() {
  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  size_bytes = 0;
}

SkipListIterator SkipList::begin() {
  // return SkipListIterator(head->forward[0], rw_mutex);
  return SkipListIterator(head->forward_[0]);
}

SkipListIterator SkipList::end() {
  return SkipListIterator(); // 使用空构造函数
}

// 找到前缀的起始位置
// 返回第一个前缀匹配或者大于前缀的迭代器
SkipListIterator SkipList::begin_preffix(const std::string &prefix) {
  auto prevs = std::vector<std::shared_ptr<SkipListNode>>(current_level, head);
  FindLessThan(prevs, prefix); // 找到最后一个小于 preffix 的节点
  auto curr = prevs[0]->forward_[0];
  int len = prefix.size();
  if(curr != nullptr && curr->key_.substr(0,len) == prefix) {
    return SkipListIterator(prevs[0]->forward_[0]);
  }
  return SkipListIterator{};
}

// 找到前缀的终结位置
SkipListIterator SkipList::end_preffix(const std::string &prefix) {
  // 找到第一个大于prefix的节点
  int len = prefix.size();
  auto prefix_next = prefix;
  if (prefix_next[len - 1] == static_cast<char>(255)) {
    prefix_next += static_cast<char>(0);
  } else {
    prefix_next[len - 1] += 1; // 假设最后一个字符不是255
  }
  auto prevs = std::vector<std::shared_ptr<SkipListNode>>(current_level, head);
  FindLessThan(prevs, prefix_next); // 找到最后一个小于 prefix_next 的节点
  auto curr = prevs[0];
  if(curr != nullptr && curr->key_.substr(0,len) == prefix) {
    return SkipListIterator(prevs[0]->forward_[0]);
  }
  return SkipListIterator{};
}

// ? 这里单调谓词的含义是, 整个数据库只会有一段连续区间满足此谓词
// ? 例如之前特化的前缀查询，以及后续可能的范围查询，都可以转化为谓词查询
// ? 返回第一个满足谓词的位置和最后一个满足谓词的迭代器
// ? 如果不存在, 范围nullptr
// ? 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// ? predicate返回值:
// ?   0: 满足谓词
// ?   >0: 不满足谓词, 需要向右移动
// ?   <0: 不满足谓词, 需要向左移动
// ! Skiplist 中的谓词查询不会进行事务id的判断, 需要上层自己进行判断
std::optional<std::pair<SkipListIterator, SkipListIterator>>
SkipList::iters_monotony_predicate(
    std::function<int(const std::string &)> predicate) {
  auto cur = head;
  bool hit = false;
  for (int level = current_level - 1; !hit && level >= 0; level --) {
    while(cur->forward_[level] != nullptr) {
      if (predicate(cur->forward_[level]->key_) == 0) {
        cur = cur->forward_[level];
        hit = true;
        break;
      } else if (predicate(cur->forward_[level]->key_) > 0){
        cur =  cur->forward_[level];
      } else {
        break;  
      }
    }
  }

  if (hit) {
    auto start = cur;
    while(start->backward_[0].lock() && predicate(start->backward_[0].lock()->key_) == 0) {
      start = start->backward_[0].lock();
    }
    auto end = cur->forward_[0];
    while(end != nullptr && predicate(end->key_) == 0) {
      end = end->forward_[0];
    }
    return std::make_optional(std::make_pair(SkipListIterator(start), SkipListIterator(end)));
  }
  return std::nullopt;
}

// ? 打印跳表, 你可以在出错时调用此函数进行调试
void SkipList::print_skiplist() {
  for (int level = 0; level < current_level; level++) {
    std::cout << "Level " << level << ": ";
    auto current = head->forward_[level];
    while (current) {
      std::cout << current->key_;
      current = current->forward_[level];
      if (current) {
        std::cout << " -> ";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
} // namespace tiny_lsm