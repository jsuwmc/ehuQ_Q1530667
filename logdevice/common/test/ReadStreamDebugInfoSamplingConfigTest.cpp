/**
 * Copyright (c) 2017-present, Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "logdevice/common/ReadStreamDebugInfoSamplingConfig.h"

#include <chrono>
#include <fstream>
#include <thread>

#include <folly/experimental/TestUtil.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>

#include "logdevice/common/FileConfigSource.h"
#include "logdevice/common/ThriftCodec.h"
#include "logdevice/common/plugin/CommonBuiltinPlugins.h"
#include "logdevice/common/plugin/PluginRegistry.h"
#include "logdevice/common/settings/UpdateableSettings-details.h"
#include "logdevice/common/test/TestUtil.h"

using namespace facebook::logdevice;
using namespace ::testing;
using namespace configuration::all_read_streams_debug_config::thrift;
namespace {

AllReadStreamsDebugConfig buildConfig(std::string csid, int64_t deadline) {
  AllReadStreamsDebugConfig config;
  *config.csid_ref() = csid;
  *config.deadline_ref() = deadline;
  return config;
}

void writeTo(const AllReadStreamsDebugConfigs& config,
             const std::string& path) {
  std::ofstream out(path);
  out << ThriftCodec::serialize<apache::thrift::SimpleJSONSerializer>(config);
}

TEST(ReadStreamDebugInfoSamplingConfigTest, ConstructionNotFoundPlugin) {
  std::shared_ptr<PluginRegistry> plugin_registry = make_test_plugin_registry();

  std::unique_ptr<ReadStreamDebugInfoSamplingConfig> fetcher =
      std::make_unique<ReadStreamDebugInfoSamplingConfig>(
          plugin_registry, "asd: asd");

  folly::Baton<> invokeCallback;
  fetcher->setUpdateCallback([&](const auto&) { invokeCallback.post(); });

  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid1"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed(""));
  EXPECT_FALSE(invokeCallback.try_wait_for(std::chrono::seconds(1)));
}

TEST(ReadStreamDebugInfoSamplingConfigTest, Construction) {
  std::shared_ptr<PluginRegistry> plugin_registry = make_test_plugin_registry();

  AllReadStreamsDebugConfigs configs;
  configs.configs_ref()->push_back(buildConfig("test-csid", 123));
  std::string serialized_configs = "data: " +
      ThriftCodec::serialize<apache::thrift::SimpleJSONSerializer>(configs);

  std::unique_ptr<ReadStreamDebugInfoSamplingConfig> fetcher =
      std::make_unique<ReadStreamDebugInfoSamplingConfig>(
          plugin_registry, serialized_configs);

  EXPECT_TRUE(fetcher->isReadStreamDebugInfoSamplingAllowed(
      "test-csid", std::chrono::seconds(120)));

  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid1"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed(""));
}

TEST(ReadStreamDebugInfoSamplingConfigTest, ExpiredDeadline) {
  std::shared_ptr<PluginRegistry> plugin_registry = make_test_plugin_registry();

  AllReadStreamsDebugConfigs configs;
  configs.configs_ref()->push_back(buildConfig("test-csid", 1));
  std::string serialized_configs = "data: " +
      ThriftCodec::serialize<apache::thrift::SimpleJSONSerializer>(configs);

  std::unique_ptr<ReadStreamDebugInfoSamplingConfig> fetcher =
      std::make_unique<ReadStreamDebugInfoSamplingConfig>(
          plugin_registry, serialized_configs);

  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid1"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed(""));
}

TEST(ReadStreamDebugInfoSamplingConfigTest, MultipleConfigs) {
  std::shared_ptr<PluginRegistry> plugin_registry = make_test_plugin_registry();

  AllReadStreamsDebugConfigs configs;
  configs.configs_ref()->push_back(buildConfig("test-csid", 1));

  configs.configs_ref()->push_back(buildConfig("test-csid1", 123));
  std::string serialized_configs = "data: " +
      ThriftCodec::serialize<apache::thrift::SimpleJSONSerializer>(configs);

  std::unique_ptr<ReadStreamDebugInfoSamplingConfig> fetcher =
      std::make_unique<ReadStreamDebugInfoSamplingConfig>(
          plugin_registry, serialized_configs);

  EXPECT_TRUE(fetcher->isReadStreamDebugInfoSamplingAllowed(
      "test-csid1", std::chrono::seconds(120)));

  // Permission denied after deadline expiration
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed(
      "test-csid1", std::chrono::seconds(124)));

  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed("test-csid"));
  EXPECT_FALSE(fetcher->isReadStreamDebugInfoSamplingAllowed(""));
}

TEST(ReadStreamDebugInfoSamplingConfigTest, CallCallbackWithConfig) {
  folly::test::TemporaryFile config(
      "ReadStreamDebugInfoSamplingConfigTest.CallCallbackWithConfig");
  auto path = folly::fs::canonical(config.path()).string();

  std::shared_ptr<PluginRegistry> plugin_registry = make_test_plugin_registry();

  AllReadStreamsDebugConfigs configs;
  configs.configs_ref()->push_back(buildConfig("test-csid", 1));
  writeTo(configs, path);

  std::unique_ptr<ReadStreamDebugInfoSamplingConfig> fetcher =
      std::make_unique<ReadStreamDebugInfoSamplingConfig>(
          plugin_registry, "file:" + path);

  AllReadStreamsDebugConfigs readConfig;
  folly::Baton<> invokeCallback;
  fetcher->setUpdateCallback([&](const auto& config) {
    readConfig = config;
    invokeCallback.post();
  });
  EXPECT_TRUE(invokeCallback.try_wait_for(std::chrono::seconds(1)));
  ASSERT_EQ(configs, readConfig);

  invokeCallback.reset();
  configs.configs_ref()->push_back(buildConfig("test-csid-2", 3));
  writeTo(configs, path);

  ASSERT_TRUE(invokeCallback.try_wait_for(
      FileConfigSource::defaultPollingInterval() * 2));
  ASSERT_EQ(configs, readConfig);
}

} // namespace
