// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gtest/gtest.h"

#include "key_client_base_test.hpp"

#include <azure/keyvault/keyvault_keys.hpp>
#include <private/key_constants.hpp>

#include <string>

using namespace Azure::Security::KeyVault::Keys::Test;
using namespace Azure::Security::KeyVault::Keys;

TEST_F(KeyVaultClientTest, GetSingleKey)
{
  KeyClient keyClient(m_keyVaultUrl, m_credential);
  std::string keyName(GetUniqueName());

  auto createKeyResponse = keyClient.CreateEcKey(CreateEcKeyOptions(keyName));
  CheckValidResponse(createKeyResponse);
  auto keyResponse = keyClient.GetKey(keyName);
  CheckValidResponse(keyResponse);
  auto key = keyResponse.Value;

  EXPECT_EQ(key.Name(), keyName);
  EXPECT_EQ(key.GetKeyType(), KeyVaultKeyType::Ec);
}

TEST_F(KeyVaultClientTest, GetPropertiesOfKeysOnePage)
{

  KeyClient keyClient(m_keyVaultUrl, m_credential);
  // Delete and purge anything before starting the test to ensure test will work
  RemoveAllKeysFromVault(keyClient);

  // Create 5 keys
  std::vector<std::string> keyNames;
  for (int counter = 0; counter < 5; counter++)
  {
    auto name = GetUniqueName();
    CreateEcKeyOptions options(name);
    keyNames.emplace_back(name);
    auto response = keyClient.CreateEcKey(options);
    CheckValidResponse(response);
  }
  // Get Key properties
  std::vector<KeyProperties> keyPropertiesList;
  GetPropertiesOfKeysOptions options;
  for (auto keyResponse = keyClient.GetPropertiesOfKeys(options); keyResponse.HasPage();
       keyResponse.MoveToNextPage())
  {
    for (auto& key : keyResponse.Items)
    {
      keyPropertiesList.emplace_back(key);
    }
  }

  EXPECT_EQ(keyNames.size(), keyPropertiesList.size());
  for (auto const& keyProperties : keyPropertiesList)
  {
    // Check names are in the keyNames list
    auto findKeyName = std::find(keyNames.begin(), keyNames.end(), keyProperties.Name);
    EXPECT_NE(findKeyName, keyNames.end());
  }
}

TEST_F(KeyVaultClientTest, GetKeysVersionsOnePage)
{
  KeyClient keyClient(m_keyVaultUrl, m_credential);

  // Create 5 key versions
  std::string keyName(GetUniqueName());
  size_t expectedVersions = 5;
  CreateEcKeyOptions createKeyOptions(keyName);
  for (size_t counter = 0; counter < expectedVersions; counter++)
  {
    auto response = keyClient.CreateEcKey(createKeyOptions);
    CheckValidResponse(response);
  }
  // Get Key versions
  std::vector<KeyProperties> keyPropertiesList;
  GetPropertiesOfKeyVersionsOptions getKeyOptions;
  for (auto keyResponse = keyClient.GetPropertiesOfKeyVersions(keyName); keyResponse.HasPage();
       keyResponse.MoveToNextPage())
  {
    for (auto& key : keyResponse.Items)
    {
      keyPropertiesList.emplace_back(key);
    }
  }

  EXPECT_EQ(expectedVersions, keyPropertiesList.size());
  for (auto const& keyProperties : keyPropertiesList)
  {
    EXPECT_EQ(keyName, keyProperties.Name);
  }
}

TEST_F(KeyVaultClientTest, GetDeletedKeysOnePage)
{
  KeyClient keyClient(m_keyVaultUrl, m_credential);

  // Create 5 keys
  std::vector<std::string> keyNames;
  for (int counter = 0; counter < 5; counter++)
  {
    auto name = GetUniqueName();
    CreateEcKeyOptions options(name);
    keyNames.emplace_back(name);
    auto response = keyClient.CreateEcKey(options);
    CheckValidResponse(response);
  }
  // Delete keys
  std::vector<DeleteKeyOperation> operations;
  for (auto const& keyName : keyNames)
  {
    operations.emplace_back(keyClient.StartDeleteKey(keyName));
  }
  // wait for all of the delete operations to complete
  for (auto& operation : operations)
  {
    operation.PollUntilDone(m_testPollingIntervalMinutes);
  }

  // Get all deleted Keys
  std::vector<std::string> deletedKeys;
  for (auto keyResponse = keyClient.GetDeletedKeys(); keyResponse.HasPage();
       keyResponse.MoveToNextPage())
  {
    for (auto& key : keyResponse.Items)
    {
      deletedKeys.emplace_back(key.Name());
    }
  }

  // Check all keys are in the deleted key list
  for (auto const& key : keyNames)
  {
    // Check names are in the keyNames list
    auto findKeyName = std::find(deletedKeys.begin(), deletedKeys.end(), key);
    EXPECT_NE(findKeyName, deletedKeys.end());
  }
}
