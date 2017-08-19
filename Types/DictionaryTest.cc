#define __STDC_FORMAT_MACROS
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <phosg/UnitTest.hh>
#include <string>
#include <unordered_map>

#include "Dictionary.hh"
#include "Strings.hh"

using namespace std;


void expect_key_missing(const DictionaryObject* d, void* k) {
  expect(!dictionary_exists(d, k));
  try {
    dictionary_at(d, k);
    expect(false);
  } catch (const out_of_range& e) { }
}

void verify_structure(const DictionaryObject* d, const char* expected_structure) {
  // remove whitespace from expected_structure
  string processed_expected_structure;
  for (const char* ch = expected_structure; *ch; ch++) {
    if (!isblank(*ch)) {
      processed_expected_structure += *ch;
    }
  }

  string actual_structure = dictionary_structure(d);
  if (processed_expected_structure != actual_structure) {
    fprintf(stderr, "structures don\'t match\n  expected (orig): %s\n  expected: %s\n  actual  : %s\n",
        expected_structure, processed_expected_structure.c_str(),
        actual_structure.c_str());
  }
  expect_eq(processed_expected_structure, actual_structure);
}

void verify_state(
    const unordered_map<BytesObject*, BytesObject*>& expected,
    const DictionaryObject* d, size_t expected_node_size,
    const char* expected_structure = NULL) {
  expect_eq(expected.size(), dictionary_size(d));
  expect_eq(expected_node_size, dictionary_node_size(d));
  for (const auto& it : expected) {
    expect_eq(it.second, dictionary_at(d, it.first));
  }

  auto missing_elements = expected;
  DictionaryObject::SlotContents item;
  while (dictionary_next_item(d, &item)) {
    BytesObject* k = reinterpret_cast<BytesObject*>(item.key);
    auto missing_it = missing_elements.find(k);
    expect_ne(missing_it, missing_elements.end());
    expect_eq(missing_it->second, item.value);
    missing_elements.erase(missing_it);
  }
  expect_eq(true, missing_elements.empty());

  if (expected_structure) {
    verify_structure(d, expected_structure);
  }
}


static size_t num_bytes_objects = 0;

static void tracked_bytes_delete(void* o) {
  num_bytes_objects--;
  free(o);
}

BytesObject* tracked_bytes_new(BytesObject* o, const uint8_t* data,
    size_t count) {
  num_bytes_objects++;
  BytesObject* b = bytes_new(o, data, count);
  b->basic.destructor = tracked_bytes_delete;
  return b;
}

BytesObject* tracked_bytes_new(const char* text) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(text);
  return tracked_bytes_new(NULL, data, strlen(text));
}


void run_basic_test() {
  printf("-- basic\n");

  DictionaryObject* d = dictionary_new(NULL,
      reinterpret_cast<size_t (*)(const void*)>(bytes_length),
      reinterpret_cast<uint8_t (*)(const void*, size_t)>(bytes_at),
      DictionaryFlag::KeysAreObjects | DictionaryFlag::ValuesAreObjects);

  expect_eq(0, num_bytes_objects);
  expect_eq(0, dictionary_size(d));

  BytesObject* k1 = tracked_bytes_new("key1");
  BytesObject* k2 = tracked_bytes_new("key2");
  BytesObject* k3 = tracked_bytes_new("key3");
  BytesObject* v0 = tracked_bytes_new("value0");
  BytesObject* v1 = tracked_bytes_new("value1");
  BytesObject* v2 = tracked_bytes_new("value2");
  BytesObject* v3 = tracked_bytes_new("value3");
  expect_eq(1, k1->basic.refcount);
  expect_eq(1, k2->basic.refcount);
  expect_eq(1, k3->basic.refcount);
  expect_eq(1, v0->basic.refcount);
  expect_eq(1, v1->basic.refcount);
  expect_eq(1, v2->basic.refcount);
  expect_eq(1, v3->basic.refcount);
  expect_eq(7, num_bytes_objects);

  dictionary_insert(d, k1, v1);
  expect_eq(1, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));
  dictionary_insert(d, k2, v2);
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));
  dictionary_insert(d, k3, v3);
  expect_eq(3, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  expect_eq(2, k1->basic.refcount);
  expect_eq(2, k2->basic.refcount);
  expect_eq(2, k3->basic.refcount);
  expect_eq(1, v0->basic.refcount);
  expect_eq(2, v1->basic.refcount);
  expect_eq(2, v2->basic.refcount);
  expect_eq(2, v3->basic.refcount);
  expect_eq(7, num_bytes_objects);

  expect_eq(v1, dictionary_at(d, k1));
  expect_eq(v2, dictionary_at(d, k2));
  expect_eq(v3, dictionary_at(d, k3));
  expect_eq(3, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  expect_eq(true, dictionary_erase(d, k2));
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));
  expect_eq(false, dictionary_erase(d, k2));
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  expect_eq(2, k1->basic.refcount);
  expect_eq(1, k2->basic.refcount);
  expect_eq(2, k3->basic.refcount);
  expect_eq(1, v0->basic.refcount);
  expect_eq(2, v1->basic.refcount);
  expect_eq(1, v2->basic.refcount);
  expect_eq(2, v3->basic.refcount);
  expect_eq(7, num_bytes_objects);

  expect_eq(v1, dictionary_at(d, k1));
  expect_key_missing(d, k2);
  expect_eq(v3, dictionary_at(d, k3));
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  dictionary_insert(d, k1, v0);
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  expect_eq(2, k1->basic.refcount);
  expect_eq(1, k2->basic.refcount);
  expect_eq(2, k3->basic.refcount);
  expect_eq(2, v0->basic.refcount);
  expect_eq(1, v1->basic.refcount);
  expect_eq(1, v2->basic.refcount);
  expect_eq(2, v3->basic.refcount);
  expect_eq(7, num_bytes_objects);

  expect_eq(v0, dictionary_at(d, k1));
  expect_key_missing(d, k2);
  expect_eq(v3, dictionary_at(d, k3));
  expect_eq(2, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));

  expect_eq(true, dictionary_erase(d, k1));
  expect_eq(1, dictionary_size(d));
  expect_eq(4, dictionary_node_size(d));
  expect_eq(true, dictionary_erase(d, k3));
  expect_eq(0, dictionary_size(d));
  expect_eq(0, dictionary_node_size(d));

  expect_eq(1, k1->basic.refcount);
  expect_eq(1, k2->basic.refcount);
  expect_eq(1, k3->basic.refcount);
  expect_eq(1, v0->basic.refcount);
  expect_eq(1, v1->basic.refcount);
  expect_eq(1, v2->basic.refcount);
  expect_eq(1, v3->basic.refcount);
  expect_eq(7, num_bytes_objects);

  delete_reference(k1);
  delete_reference(k2);
  delete_reference(k3);
  delete_reference(v0);
  delete_reference(v1);
  delete_reference(v2);
  delete_reference(v3);
  expect_eq(0, num_bytes_objects);
}

void run_reorganization_test() {
  printf("-- reorganization\n");

  DictionaryObject* d = dictionary_new(NULL,
      reinterpret_cast<size_t (*)(const void*)>(bytes_length),
      reinterpret_cast<uint8_t (*)(const void*, size_t)>(bytes_at),
      DictionaryFlag::KeysAreObjects | DictionaryFlag::ValuesAreObjects);

  expect_eq(0, dictionary_size(d));
  expect_eq(0, num_bytes_objects);

  // initial state: empty
  unordered_map<BytesObject*, BytesObject*> expected_state;
  verify_state(expected_state, d, 0, "()");

  // <> null
  //   a null
  //     b null
  //       (c) "abc"
  BytesObject* abc = tracked_bytes_new("abc");
  dictionary_insert(d, abc, abc);
  expected_state.emplace(abc, abc);
  verify_state(expected_state, d, 3,
      "([61,61]@00+#,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:V)))");
  expect_eq(3, abc->basic.refcount);

  // <> null
  //   a null
  //     b "ab"
  //       (c) "abc"
  BytesObject* ab = tracked_bytes_new("ab");
  dictionary_insert(d, ab, ab);
  expected_state.emplace(ab, ab);
  verify_state(expected_state, d, 3,
      "([61,61]@00+#,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+V,"
      "    63:V)))");
  expect_eq(3, ab->basic.refcount);

  // <> null
  //   a null
  //     (b) "ab"
  dictionary_erase(d, abc);
  expected_state.erase(abc);
  verify_state(expected_state, d, 2,
      "([61,61]@00+#,"
      "61:("
      "  [62,62]@61+#,"
      "  62:V))");
  expect_eq(1, abc->basic.refcount);

  // <> ""
  //   a null
  //     (b) "ab"
  BytesObject* blank = tracked_bytes_new("");
  dictionary_insert(d, blank, blank);
  expected_state.emplace(blank, blank);
  verify_state(expected_state, d, 2,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:V))");
  expect_eq(3, blank->basic.refcount);

  // <> ""
  //   a null
  //     b "ab"
  //       c null
  //         (d) "abcd"
  BytesObject* abcd = tracked_bytes_new("abcd");
  dictionary_insert(d, abcd, abcd);
  expected_state.emplace(abcd, abcd);
  verify_state(expected_state, d, 4,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+V,"
      "    63:("
      "      [64,64]@63+#,"
      "      64:V))))");
  expect_eq(3, abcd->basic.refcount);

  // <> ""
  //   a null
  //     b null
  //       c null
  //         (d) "abcd"
  dictionary_erase(d, ab);
  expected_state.erase(ab);
  verify_state(expected_state, d, 4,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:("
      "      [64,64]@63+#,"
      "      64:V))))");
  expect_eq(1, ab->basic.refcount);

  // <> ""
  //   a null
  //     b null
  //       c null
  //         d "abcd"
  //           (e) "abcde"
  BytesObject* abcde = tracked_bytes_new("abcde");
  dictionary_insert(d, abcde, abcde);
  expected_state.emplace(abcde, abcde);
  verify_state(expected_state, d, 5,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:("
      "      [64,64]@63+#,"
      "      64:("
      "        [65,65]@64+V,"
      "        65:V)))))");
  expect_eq(3, abcde->basic.refcount);

  // <> ""
  //   a null
  //     b null
  //       c null
  //         d "abcd"
  //           (e) "abcde"
  //           (f) "abcdf"
  BytesObject* abcdf = tracked_bytes_new("abcdf");
  dictionary_insert(d, abcdf, abcdf);
  expected_state.emplace(abcdf, abcdf);
  verify_state(expected_state, d, 5,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:("
      "      [64,64]@63+#,"
      "      64:("
      "        [65,66]@64+V,"
      "        65:V,"
      "        66:V)))))");
  expect_eq(3, abcdf->basic.refcount);

  // <> ""
  //   a null
  //     b null
  //       c null
  //         d "abcd"
  //           (e) "abcde"
  //           (f) "abcdf"
  //         (e) "abce"
  BytesObject* abce = tracked_bytes_new("abce");
  dictionary_insert(d, abce, abce);
  expected_state.emplace(abce, abce);
  verify_state(expected_state, d, 5,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:("
      "      [64,65]@63+#,"
      "      64:("
      "        [65,66]@64+V,"
      "        65:V,"
      "        66:V),"
      "      65:V))))");
  expect_eq(3, abce->basic.refcount);

  // <> ""
  //   a null
  //     b null
  //       c null
  //         d "abcd"
  //           (e) "abcde"
  //           (f) "abcdf"
  //         e "abce"
  //           (f) "abcef"
  BytesObject* abcef = tracked_bytes_new("abcef");
  dictionary_insert(d, abcef, abcef);
  expected_state.emplace(abcef, abcef);
  verify_state(expected_state, d, 6,
      "([61,61]@00+V,"
      "61:("
      "  [62,62]@61+#,"
      "  62:("
      "    [63,63]@62+#,"
      "    63:("
      "      [64,65]@63+#,"
      "      64:("
      "        [65,66]@64+V,"
      "        65:V,"
      "        66:V),"
      "      65:("
      "        [66,66]@65+V,"
      "        66:V)))))");
  expect_eq(3, abcef->basic.refcount);

  // <> null
  expect_eq(8, num_bytes_objects);
  dictionary_clear(d);
  for (const auto& it : expected_state) {
    expect_eq(it.first, it.second);
    expect_eq(1, it.first->basic.refcount);
  }
  delete_reference(abc);
  delete_reference(ab);
  delete_reference(blank);
  delete_reference(abcd);
  delete_reference(abcde);
  delete_reference(abcdf);
  delete_reference(abce);
  delete_reference(abcef);
  expected_state.clear();
  verify_state(expected_state, d, 0, "()");

  expect_eq(0, num_bytes_objects);
}


int main(int argc, char* argv[]) {
  run_basic_test();
  run_reorganization_test();
  printf("all tests passed\n");
  return 0;
}
