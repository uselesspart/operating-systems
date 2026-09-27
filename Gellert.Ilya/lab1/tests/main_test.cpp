#include <gtest/gtest.h>
#include "config/config.hpp"
#include "worker/worker.hpp"
#include <fstream>
#include <filesystem>

class ConfigParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        testConfigPath = "test_config.conf";
    }

    void TearDown() override {
        std::filesystem::remove(testConfigPath);
    }

    void createConfig(const std::string& content) {
        std::ofstream out(testConfigPath);
        out << content;
        out.close();
    }

    std::string testConfigPath;
};

TEST_F(ConfigParserTest, ParseValidConfig) {
    createConfig(
        "# Comment\n"
        "DIR=/tmp/test1\n"
        "  DIR = /var/log/test2  \n"
    );

    auto config = monitor::ConfigParser::load(testConfigPath);
    
    ASSERT_EQ(config.watchDirs.size(), 2);
    EXPECT_EQ(config.watchDirs[0], "/tmp/test1");
    EXPECT_EQ(config.watchDirs[1], "/var/log/test2");
}

TEST_F(ConfigParserTest, ThrowOnEmptyDirs) {
    createConfig(
        "# Only comments\n"
        "INVALID_KEY=123\n"
    );

    EXPECT_THROW(monitor::ConfigParser::load(testConfigPath), std::runtime_error);
}

TEST(WorkerTest, InitValidDirectory) {
    std::string testDir = "/tmp/worker_test_dir";
    std::filesystem::create_directory(testDir);

    monitor::Worker worker;
    std::vector<std::string> dirs = { testDir };
    
    EXPECT_NO_THROW(worker.init(dirs));

    std::filesystem::remove(testDir);
}