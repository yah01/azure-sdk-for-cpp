﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/**
 * @file
 * @brief The base class to construct and init a Key Vault client.
 *
 */

#include <gtest/gtest.h>

#include "../src/private/certificate_serializers.hpp"
#include <azure/core/test/test_base.hpp>
#include <azure/identity/client_secret_credential.hpp>
#include <azure/keyvault/keyvault_certificates.hpp>
#include <thread>

using namespace std::chrono_literals;

namespace Azure {
  namespace Security {
    namespace KeyVault {
      namespace Certificates {
        namespace Test {

  /**
   * @brief A certificate downloaded X509 data.
   *
   */
  struct DownloadCertificateResult final
  {
    /**
     * @brief Certificate data.
     *
     */
    std::string Certificate;

    /**
     * @brief Content Type.
     *
     */
    CertificateContentType ContentType;
  };
  /**
   * @brief Client Certificate Credential authenticates with the Azure services using a
   * Tenant ID, Client ID and a client secret.
   *
   */
  class TestClientSecretCredential final : public Core::Credentials::TokenCredential {
  public:
    Core::Credentials::AccessToken GetToken(
        Core::Credentials::TokenRequestContext const& tokenRequestContext,
        Core::Context const& context) const override
    {
      Core::Credentials::AccessToken accessToken;
      accessToken.Token = "magicToken";
      accessToken.ExpiresOn = DateTime::max();

      if (context.IsCancelled() || tokenRequestContext.Scopes.size() == 0)
      {
        accessToken.ExpiresOn = DateTime::min();
      }

      return accessToken;
    }
  };

  class KeyVaultCertificateClientTest : public Azure::Core::Test::TestBase,
                                        public ::testing::WithParamInterface<int> {
  private:
    std::unique_ptr<Azure::Security::KeyVault::Certificates::CertificateClient> m_client;
    std::string GetEnv(const std::string& name, std::string const& defaultValue = std::string())
    {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
      const char* ret = std::getenv(name.data());
#pragma warning(pop)
#else
      const char* ret = std::getenv(name.data());
#endif

      if (!ret)
      {
        if (!defaultValue.empty())
        {
          return defaultValue;
        }

        throw std::runtime_error(
            name + " is required to run the tests but not set as an environment variable.");
      }

      return std::string(ret);
    }

  protected:
    std::shared_ptr<Azure::Identity::ClientSecretCredential> m_credential;
    std::shared_ptr<TestClientSecretCredential> m_testCredential;
    std::string m_keyVaultUrl;
    std::chrono::milliseconds m_defaultWait;

    Azure::Security::KeyVault::Certificates::CertificateClient const& GetClientForTest(
        std::string const& testName)
    {
      // used to test/dev purposes _putenv_s("AZURE_TEST_MODE", "LIVE");
      InitializeClient();
      // set the interceptor for the current test
      m_testContext.RenameTest(testName);
      return *m_client;
    }

    // Create
    void InitializeClient()
    {
      // Init interceptor from PlayBackRecorder
      std::string recordingPath(AZURE_TEST_RECORDING_DIR);
      recordingPath.append("/recordings");
      Azure::Core::Test::TestBase::SetUpBase(recordingPath);

      std::string tenantId = GetEnv("AZURE_TENANT_ID", "tenant");
      std::string clientId = GetEnv("AZURE_CLIENT_ID", "client");
      std::string secretId = GetEnv("AZURE_CLIENT_SECRET", "secret");

      m_keyVaultUrl = GetEnv("AZURE_KEYVAULT_URL", "https://REDACTED.vault.azure.net");

      // Create default client for the test
      CertificateClientOptions options;
      // Replace default transport adapter for playback
      if (m_testContext.IsPlaybackMode())
      {
        options.Transport.Transport = m_interceptor->GetPlaybackClient();
      }
      // Insert Recording policy when Record mode is on (non playback and non LiveMode)
      else if (!m_testContext.IsLiveMode())
      {
        // AZURE_TEST_RECORDING_DIR is exported by CMAKE
        options.PerRetryPolicies.push_back(m_interceptor->GetRecordPolicy());
      }

      if (m_testContext.IsPlaybackMode())
      { // inject fake token client here if it's test
        m_testCredential = std::make_shared<TestClientSecretCredential>();
        m_client = std::make_unique<CertificateClient>(m_keyVaultUrl, m_testCredential, options);
        // we really dont need to wait for results
        m_defaultWait = 1ms;
        m_keyVaultUrl = "https://REDACTED.vault.azure.net";
      }
      else
      {
        m_credential = std::make_shared<Azure::Identity::ClientSecretCredential>(
            tenantId, clientId, secretId);
        m_client = std::make_unique<CertificateClient>(m_keyVaultUrl, m_credential, options);
        m_defaultWait = 20s;
      }

      // When running live tests, service can return 429 error response if the client is
      // sending multiple requests per second. This can happen if the network is fast and
      // tests are running without any delay between them.
      auto avoidTestThrottled = GetEnv("AZURE_KEYVAULT_AVOID_THROTTLED", "0");

      if (avoidTestThrottled != "0")
      {
        std::cout << "- Wait to avoid server throttled..." << std::endl;
        // 10 sec should be enough to prevent from 429 error
        std::this_thread::sleep_for(std::chrono::seconds(10));
      }
    }

  public:
    template <class T>
    static inline void CheckValidResponse(
        Azure::Response<T>& response,
        Azure::Core::Http::HttpStatusCode expectedCode = Azure::Core::Http::HttpStatusCode::Ok)
    {
      auto const& rawResponse = response.RawResponse;
      EXPECT_EQ(
          static_cast<typename std::underlying_type<Azure::Core::Http::HttpStatusCode>::type>(
              rawResponse->GetStatusCode()),
          static_cast<typename std::underlying_type<Azure::Core::Http::HttpStatusCode>::type>(
              expectedCode));
    }

    static void CheckIssuers(CertificateIssuer const& data, CertificateIssuer const& issuer)
    {
      EXPECT_EQ(data.Name, issuer.Name);
      EXPECT_EQ(data.Provider.Value(), issuer.Provider.Value());
      EXPECT_TRUE(data.Properties.Enabled.Value());
      EXPECT_TRUE(data.IdUrl);

      EXPECT_EQ(data.Credentials.AccountId.Value(), issuer.Credentials.AccountId.Value());
      EXPECT_FALSE(data.Credentials.Password);

      auto adminRemote = data.Organization.AdminDetails[0];
      auto adminLocal = issuer.Organization.AdminDetails[0];

      EXPECT_EQ(adminLocal.EmailAddress.Value(), adminRemote.EmailAddress.Value());
      EXPECT_EQ(adminLocal.FirstName.Value(), adminRemote.FirstName.Value());
      EXPECT_EQ(adminLocal.LastName.Value(), adminRemote.LastName.Value());
      EXPECT_EQ(adminLocal.PhoneNumber.Value(), adminRemote.PhoneNumber.Value());
    }

    static inline void CheckContactsCollections(
        std::vector<CertificateContact> contacts,
        std::vector<CertificateContact> results)
    {
      EXPECT_EQ(results.size(), contacts.size());

      for (auto c2 : results)
      {
        bool found = false;
        for (auto c1 : contacts)
        {
          if (c1.EmailAddress == c2.EmailAddress && c1.Name.HasValue() == c2.Name.HasValue()
              && c1.Phone.HasValue() == c2.Phone.HasValue())
          {
            found = true;
            break;
          }
        }
        EXPECT_TRUE(found);
      }

      for (auto c1 : contacts)
      {
        bool found = false;
        for (auto c2 : results)
        {
          if (c1.EmailAddress == c2.EmailAddress && c1.Name.HasValue() == c2.Name.HasValue()
              && c1.Phone.HasValue() == c2.Phone.HasValue())
          {
            found = true;
            break;
          }
        }
        EXPECT_TRUE(found);
      }
    }

    static inline KeyVaultCertificateWithPolicy CreateCertificate(
        std::string const& name,
        CertificateClient const& client,
        std::chrono::milliseconds defaultWait,
        std::string const& subject = "CN=xyz",
        CertificateContentType certificateType = CertificateContentType::Pkcs12)
    {
      CertificateCreateOptions options;
      options.Policy.Subject = subject;
      options.Policy.ValidityInMonths = 12;
      options.Policy.Enabled = true;

      options.Properties.Enabled = true;
      options.Properties.Name = name;
      options.Policy.ContentType = certificateType;
      options.Policy.IssuerName = "Self";

      LifetimeAction action;
      action.LifetimePercentage = 80;
      action.Action = CertificatePolicyAction::AutoRenew;
      options.Policy.LifetimeActions.emplace_back(action);

      auto response = client.StartCreateCertificate(name, options);
      auto result = response.PollUntilDone(defaultWait);

      EXPECT_EQ(result.Value.Name(), options.Properties.Name);
      EXPECT_EQ(result.Value.Properties.Name, options.Properties.Name);
      EXPECT_EQ(result.Value.Properties.Enabled.Value(), true);
      EXPECT_EQ(result.Value.Policy.IssuerName.Value(), options.Policy.IssuerName.Value());
      EXPECT_EQ(result.Value.Policy.ContentType.Value(), options.Policy.ContentType.Value());
      EXPECT_EQ(result.Value.Policy.Subject, options.Policy.Subject);
      EXPECT_EQ(
          result.Value.Policy.ValidityInMonths.Value(), options.Policy.ValidityInMonths.Value());
      EXPECT_EQ(result.Value.Policy.Enabled.Value(), options.Policy.Enabled.Value());
      EXPECT_EQ(result.Value.Policy.LifetimeActions.size(), size_t(1));
      EXPECT_EQ(result.Value.Policy.LifetimeActions[0].Action, action.Action);
      EXPECT_EQ(
          result.Value.Policy.LifetimeActions[0].LifetimePercentage.Value(),
          action.LifetimePercentage.Value());
      EXPECT_EQ(result.Value.Policy.KeyUsage.size(), size_t(2));
      auto keyUsage = result.Value.Policy.KeyUsage;
      EXPECT_TRUE(
          (keyUsage[0] == CertificateKeyUsage::DigitalSignature
           && keyUsage[1] == CertificateKeyUsage::KeyEncipherment)
          || (keyUsage[1] == CertificateKeyUsage::DigitalSignature
              && keyUsage[0] == CertificateKeyUsage::KeyEncipherment));
      return result.Value;
    }

    Azure::Response<DownloadCertificateResult> DownloadCertificate(
        std::string const& name,
        CertificateClient const& client,
        Azure::Core::Context const& context = Azure::Core::Context()) const
    {
      {
        KeyVaultCertificateWithPolicy certificate;
        auto response = client.GetCertificate(name, context);
        certificate = response.Value;

        Azure::Core::Url url(certificate.SecretIdUrl);
        auto secretRequest
            = client.CreateRequest(Azure::Core::Http::HttpMethod::Get, {url.GetPath()});

        auto secretResponse = client.SendRequest(secretRequest, context);
        auto secret = _detail::KeyVaultSecretSerializer::Deserialize(*secretResponse);

        DownloadCertificateResult result{secret.Value, secret.ContentType.Value()};
        return Azure::Response<DownloadCertificateResult>(
            std::move(result), std::move(secretResponse));
      }
    }
  };
}}}}} // namespace Azure::Security::KeyVault::Certificates::Test
