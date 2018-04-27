//
//  MHashTreeTests.cc
//  Fleece
//
//  Copyright © 2018 Couchbase. All rights reserved.
//

#include "FleeceTests.hh"
#include "Fleece.hh"
#include "MHashTree.hh"
#include "Encoder.hh"

using namespace fleece;

static const char* kDigits[10] = {"zero", "one", "two", "three", "four", "five", "six",
                                  "seven", "eight", "nine"};


class HashTreeTests {
public:
    MHashTree tree;
    std::vector<alloc_slice> keys;
    const Array* values;
    alloc_slice _valueBuf;

    void createItems(size_t N) {
        Encoder enc;
        enc.beginArray(N);
        for (size_t i = 0; i < N; i++)
            enc.writeInt(i);
        enc.endArray();
        _valueBuf = enc.extractOutput();
        values = Value::fromTrustedData(_valueBuf)->asArray();

        keys.clear();
        for (size_t i = 0; i < N; i++) {
            char buf[100];
            if (i < 100)
                sprintf(buf, "%s %s", kDigits[i/10], kDigits[i%10]);
            else
                sprintf(buf, "%zd %s", i/10, kDigits[i%10]);
            keys.push_back(alloc_slice(buf));
        }
    }

    void insertItems(size_t N =0, bool verbose= false, bool check =false) {
        if (N == 0)
            N = keys.size();
        for (size_t i = 0; i < N; i++) {
            if (verbose)
                std::cerr << "\n##### Inserting #" << (i)
                          << ", " << std::hex << keys[i].hash() << "\n";
            tree.insert(keys[i], values->get(uint32_t(i)));
            if (verbose)
                tree.dump(std::cerr);
            if (check) {
                CHECK(tree.count() == i + 1);
                for (ssize_t j = i; j >= 0; --j)
                    CHECK(tree.get(keys[j]) == values->get(uint32_t(i)));
            }
        }
    }

    void checkTree(size_t N) {
        CHECK(tree.count() == N);
        for (size_t i = 0; i < N; i++) {
            auto value = tree.get(keys[i]);
            REQUIRE(value);
            CHECK(value->isInteger());
            CHECK(value->asInt() == values->get(uint32_t(i))->asInt());
        }
    }

    alloc_slice encodeTree() {
        Encoder enc;
        enc.suppressTrailer();
        tree.writeTo(enc);
        return enc.extractOutput();
    }

};


#pragma mark - TEST CASES:



TEST_CASE_METHOD(HashTreeTests, "Empty MHashTree", "[HashTree]") {
    CHECK(tree.count() == 0);
    CHECK(tree.get(alloc_slice("foo")) == 0);
    CHECK(!tree.remove(alloc_slice("foo")));
}


TEST_CASE_METHOD(HashTreeTests, "Tiny MHashTree Insert", "[HashTree]") {
    createItems(1);
    auto key = keys[0];
    auto val = values->get(0);
    tree.insert(key, val);

    CHECK(tree.get(key) == val);
    CHECK(tree.count() == 1);

    tree.dump(std::cerr);
}


TEST_CASE_METHOD(HashTreeTests, "Bigger MHashTree Insert", "[HashTree]") {
    static constexpr int N = 1000;
    createItems(N);
    insertItems();
//    tree.dump(std::cerr);
    checkTree(N);
}


TEST_CASE_METHOD(HashTreeTests, "Tiny MHashTree Remove", "[HashTree]") {
    createItems(1);
    auto key = keys[0];
    auto val = values->get(0);

    tree.insert(key, val);
    CHECK(tree.remove(key));
    CHECK(tree.get(key) == 0);
    CHECK(tree.count() == 0);
}


TEST_CASE_METHOD(HashTreeTests, "Bigger MHashTree Remove", "[HashTree]") {
    static constexpr int N = 10000;
    createItems(N);
    insertItems();

    for (int i = 0; i < N; i += 3) {
        tree.remove(keys[i]);
    }
    for (int i = 0; i < N; i++) {
        CHECK(tree.get(keys[i]) == ((i%3) ? values->get(uint32_t(i)) : nullptr));
    }
    CHECK(tree.count() == N - 1 - (N / 3));
}


TEST_CASE_METHOD(HashTreeTests, "Tiny MHashTree Write", "[HashTree]") {
    createItems(10);
    auto key = keys[8];
    auto val = values->get(8);
    tree.insert(key, val);

    alloc_slice data = encodeTree();
    REQUIRE(data.size == 30); // could change if encoding changes
    std::cerr << data.size << " bytes encoded: " << data.hexString() << "\n";

    // Now read it as an immutable HashTree:
    const HashTree *tree = HashTree::fromData(data);
    CHECK(tree->count() == 1);
    auto value = tree->get(key);
    REQUIRE(value);
    CHECK(value->isInteger());
    CHECK(value->asInt() == 8);
}


TEST_CASE_METHOD(HashTreeTests, "Bigger MHashTree Write", "[HashTree]") {
    static constexpr int N = 100;
    createItems(N);
    insertItems();

    alloc_slice data = encodeTree();
//    std::cerr << "Data: " << data.hexString() << "\noffset = " << offset << " of " << data.size << "\n";

    // Now read it as an immutable HashTree:
    const HashTree *itree = HashTree::fromData(data);
    CHECK(itree->count() == N);
}


TEST_CASE_METHOD(HashTreeTests, "Tiny HashTree Mutate", "[HashTree]") {
    createItems(10);
    tree.insert(keys[9], values->get(9));

    alloc_slice data = encodeTree();
    const HashTree *itree = HashTree::fromData(data);
    itree->dump(std::cerr);

    // Wrap in a MHashTree and get the key:
    tree = itree;
    
    tree.dump(std::cerr);
    CHECK(tree.count() == 1);
    auto value = tree.get(keys[9]);
    REQUIRE(value);
    CHECK(value->isInteger());
    CHECK(value->asInt() == 9);

    // Modify the value for the key:
    tree.insert(keys[9], values->get(3));

    tree.dump(std::cerr);
    CHECK(tree.count() == 1);
    value = tree.get(keys[9]);
    REQUIRE(value);
    CHECK(value->asInt() == 3);
}


TEST_CASE_METHOD(HashTreeTests, "Bigger HashTree Mutate by replacing", "[HashTree]") {
    createItems(100);
    insertItems(100);

    alloc_slice data = encodeTree();
    const HashTree *itree = HashTree::fromData(data);
//    itree->dump(std::cerr);

    // Wrap in a MHashTree and get the key:
    tree = itree;

//    tree.dump(std::cerr);
    checkTree(100);

    for (int i = 0; i < 10; ++i) {
        // Modify the value for the key:
        int old = i*i, nuu = 99-old;
        //std::cerr << "\n#### Set key " << old << " to " << nuu << ":\n";
        tree.insert(keys[old], values->get(nuu));

        //tree.dump(std::cerr);
        CHECK(tree.count() == 100);
        auto value = tree.get(keys[old]);
        REQUIRE(value);
        CHECK(value->asInt() == nuu);
    }
}


TEST_CASE_METHOD(HashTreeTests, "Bigger HashTree Mutate by inserting", "[HashTree]") {
    createItems(20);
    insertItems(10);

    alloc_slice data = encodeTree();
    const HashTree *itree = HashTree::fromData(data);
    tree = itree;
    checkTree(10);

//    std::cerr << "#### Before:\n";
//    tree.dump(std::cerr);

    for (int i = 10; i < 20; i++) {
        //std::cerr << "\n#### Add " << i << ":\n";
        tree.insert(keys[i], values->get(uint32_t(i)));
//        tree.dump(std::cerr);
        checkTree(i+1);
    }

    for (int i = 0; i <= 5; ++i) {
//        std::cerr << "\n#### Remove " << (3*i + 2) << ":\n";
        CHECK(tree.remove(keys[3*i + 2]));
//        tree.dump(std::cerr);
        CHECK(tree.count() == 19 - i);
    }
    tree.dump(std::cerr);
}


TEST_CASE_METHOD(HashTreeTests, "HashTree Re-Encode Delta", "[HashTree]") {
    static const unsigned N = 50;
    createItems(2*N);
    insertItems(N);

    alloc_slice data = encodeTree();
    const HashTree *itree = HashTree::fromData(data);
    tree = itree;

    for (unsigned i = N; i < N + 10; i++)
        tree.insert(keys[i], values->get(uint32_t(i)));
    for (unsigned i = 2; i < N + 5; i += 3)
        CHECK(tree.remove(keys[i]));

    tree.dump(std::cerr);

    Encoder enc;
    enc.setBase(data);
    enc.suppressTrailer();
    tree.writeTo(enc);
    alloc_slice delta = enc.extractOutput();

    std::cerr << "Original is " << data.size << " bytes encoded:\t" << data.hexString() << "\n";
    std::cerr << "Delta is " << delta.size << " bytes encoded:\t" << data.hexString() << "\n";

    alloc_slice full = encodeTree();
    std::cerr << "Full rewrite would be " << full.size << " bytes encoded.\n";

    alloc_slice total(data.size + delta.size);
    memcpy((void*)&total[0],         data.buf, data.size);
    memcpy((void*)&total[data.size], delta.buf, delta.size);

    itree = HashTree::fromData(total);
    std::cerr << "\nFinal immutable tree:\n";
    itree->dump(std::cerr);
}
