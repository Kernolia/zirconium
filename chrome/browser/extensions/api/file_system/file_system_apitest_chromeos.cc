// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/file_system/file_system_api.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "chrome/browser/apps/app_browsertest_util.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/file_manager/volume_manager.h"
#include "chrome/browser/chromeos/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/features/feature_channel.h"
#include "content/public/test/test_utils.h"
#include "google_apis/drive/drive_api_parser.h"
#include "google_apis/drive/test_util.h"
#include "storage/browser/fileapi/external_mount_points.h"
#include "ui/base/ui_base_types.h"

using file_manager::VolumeManager;

namespace extensions {
namespace {

// Mount point names for chrome.fileSystem.requestFileSystem() tests.
const char kWritableMountPointName[] = "writable";
const char kReadOnlyMountPointName[] = "read-only";

// Child directory created in each of the mount points.
const char kChildDirectory[] = "child-dir";

}  // namespace

// Skips the user consent dialog for chrome.fileSystem.requestFileSystem() and
// simulates clicking of the specified dialog button.
class ScopedSkipRequestFileSystemDialog {
 public:
  explicit ScopedSkipRequestFileSystemDialog(ui::DialogButton button) {
    FileSystemRequestFileSystemFunction::SetAutoDialogButtonForTest(button);
  }
  ~ScopedSkipRequestFileSystemDialog() {
    FileSystemRequestFileSystemFunction::SetAutoDialogButtonForTest(
        ui::DIALOG_BUTTON_NONE);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedSkipRequestFileSystemDialog);
};

// This class contains chrome.filesystem API test specific to Chrome OS, namely,
// the integrated Google Drive support.
class FileSystemApiTestForDrive : public PlatformAppBrowserTest {
 public:
  FileSystemApiTestForDrive()
      : fake_drive_service_(NULL),
        integration_service_(NULL) {
  }

  // Sets up fake Drive service for tests (this has to be injected before the
  // real DriveIntegrationService instance is created.)
  void SetUpInProcessBrowserTestFixture() override {
    PlatformAppBrowserTest::SetUpInProcessBrowserTestFixture();
    extensions::ComponentLoader::EnableBackgroundExtensionsForTesting();

    ASSERT_TRUE(test_cache_root_.CreateUniqueTempDir());

    create_drive_integration_service_ =
        base::Bind(&FileSystemApiTestForDrive::CreateDriveIntegrationService,
                   base::Unretained(this));
    service_factory_for_test_.reset(
        new drive::DriveIntegrationServiceFactory::ScopedFactoryForTest(
            &create_drive_integration_service_));
  }

  // Ensure the fake service's data is fetch in the local file system. This is
  // necessary because the fetch starts lazily upon the first read operation.
  void SetUpOnMainThread() override {
    PlatformAppBrowserTest::SetUpOnMainThread();

    scoped_ptr<drive::ResourceEntry> entry;
    drive::FileError error = drive::FILE_ERROR_FAILED;
    integration_service_->file_system()->GetResourceEntry(
        base::FilePath::FromUTF8Unsafe("drive/root"),  // whatever
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    content::RunAllBlockingPoolTasksUntilIdle();
    ASSERT_EQ(drive::FILE_ERROR_OK, error);
  }

  void TearDown() override {
    FileSystemChooseEntryFunction::StopSkippingPickerForTest();
    PlatformAppBrowserTest::TearDown();
  };

 private:
  drive::DriveIntegrationService* CreateDriveIntegrationService(
      Profile* profile) {
    // Ignore signin profile.
    if (profile->GetPath() == chromeos::ProfileHelper::GetSigninProfileDir())
      return NULL;

    // FileSystemApiTestForDrive doesn't expect that several user profiles could
    // exist simultaneously.
    DCHECK(fake_drive_service_ == NULL);
    fake_drive_service_ = new drive::FakeDriveService;
    fake_drive_service_->LoadAppListForDriveApi("drive/applist.json");

    SetUpTestFileHierarchy();

    integration_service_ = new drive::DriveIntegrationService(
        profile, NULL, fake_drive_service_, std::string(),
        test_cache_root_.path(), NULL);
    return integration_service_;
  }

  void SetUpTestFileHierarchy() {
    const std::string root = fake_drive_service_->GetRootResourceId();
    ASSERT_TRUE(AddTestFile("open_existing.txt", "Can you see me?", root));
    ASSERT_TRUE(AddTestFile("open_existing1.txt", "Can you see me?", root));
    ASSERT_TRUE(AddTestFile("open_existing2.txt", "Can you see me?", root));
    ASSERT_TRUE(AddTestFile("save_existing.txt", "Can you see me?", root));
    const std::string subdir = AddTestDirectory("subdir", root);
    ASSERT_FALSE(subdir.empty());
    ASSERT_TRUE(AddTestFile("open_existing.txt", "Can you see me?", subdir));
  }

  bool AddTestFile(const std::string& title,
                   const std::string& data,
                   const std::string& parent_id) {
    scoped_ptr<google_apis::FileResource> entry;
    google_apis::DriveApiErrorCode error = google_apis::DRIVE_OTHER_ERROR;
    fake_drive_service_->AddNewFile(
        "text/plain", data, parent_id, title, false,
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    content::RunAllPendingInMessageLoop();
    return error == google_apis::HTTP_CREATED && entry;
  }

  std::string AddTestDirectory(const std::string& title,
                               const std::string& parent_id) {
    scoped_ptr<google_apis::FileResource> entry;
    google_apis::DriveApiErrorCode error = google_apis::DRIVE_OTHER_ERROR;
    fake_drive_service_->AddNewDirectory(
        parent_id, title, drive::AddNewDirectoryOptions(),
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    content::RunAllPendingInMessageLoop();
    return error == google_apis::HTTP_CREATED && entry ? entry->file_id() : "";
  }

  base::ScopedTempDir test_cache_root_;
  drive::FakeDriveService* fake_drive_service_;
  drive::DriveIntegrationService* integration_service_;
  drive::DriveIntegrationServiceFactory::FactoryCallback
      create_drive_integration_service_;
  scoped_ptr<drive::DriveIntegrationServiceFactory::ScopedFactoryForTest>
      service_factory_for_test_;
};

// This class contains chrome.filesystem.requestFileSystem API tests.
class FileSystemApiTestForRequestFileSystem : public PlatformAppBrowserTest {
 public:
  FileSystemApiTestForRequestFileSystem()
      : current_channel_(chrome::VersionInfo::CHANNEL_DEV),
        fake_user_manager_(nullptr) {}

  void SetUpOnMainThread() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    CreateTestingFileSystem(kWritableMountPointName, false /* read_only */);
    CreateTestingFileSystem(kReadOnlyMountPointName, true /* read_only */);
    PlatformAppBrowserTest::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    PlatformAppBrowserTest::TearDownOnMainThread();
    user_manager_enabler_.reset();
    fake_user_manager_ = nullptr;
  }

 protected:
  extensions::ScopedCurrentChannel current_channel_;
  base::ScopedTempDir temp_dir_;
  chromeos::FakeChromeUserManager* fake_user_manager_;
  scoped_ptr<chromeos::ScopedUserManagerEnabler> user_manager_enabler_;

  // Creates a testing file system in a testing directory.
  void CreateTestingFileSystem(const std::string& mount_point_name,
                               bool read_only) {
    const base::FilePath mount_point_path =
        temp_dir_.path().Append(mount_point_name);
    LOG(ERROR) << mount_point_path.value();
    ASSERT_TRUE(base::CreateDirectory(mount_point_path));
    ASSERT_TRUE(
        base::CreateDirectory(mount_point_path.Append(kChildDirectory)));
    ASSERT_TRUE(content::BrowserContext::GetMountPoints(browser()->profile())
                    ->RegisterFileSystem(
                        mount_point_name, storage::kFileSystemTypeNativeLocal,
                        storage::FileSystemMountOption(), mount_point_path));
    VolumeManager* const volume_manager =
        VolumeManager::Get(browser()->profile());
    ASSERT_TRUE(volume_manager);
    volume_manager->AddVolumeForTesting(
        mount_point_path, file_manager::VOLUME_TYPE_TESTING,
        chromeos::DEVICE_TYPE_UNKNOWN, read_only);
  }

  // Simulates entering the kiosk session.
  void EnterKioskSession() {
    fake_user_manager_ = new chromeos::FakeChromeUserManager();
    user_manager_enabler_.reset(
        new chromeos::ScopedUserManagerEnabler(fake_user_manager_));

    const std::string kKioskLogin = "kiosk@foobar.com";
    fake_user_manager_->AddKioskAppUser(kKioskLogin);
    fake_user_manager_->LoginUser(kKioskLogin);
  }
};

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenExistingFileTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/open_existing.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/open_existing"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenExistingFileWithWriteTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/open_existing.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/open_existing_with_write")) << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenMultipleSuggested) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/open_existing.txt");
  ASSERT_TRUE(PathService::OverrideAndCreateIfNeeded(
      chrome::DIR_USER_DOCUMENTS, test_file.DirName(), true, false));
  FileSystemChooseEntryFunction::SkipPickerAndSelectSuggestedPathForTest();
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/open_multiple_with_suggested_name"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenMultipleExistingFilesTest) {
  base::FilePath test_file1 = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/open_existing1.txt");
  base::FilePath test_file2 = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/open_existing2.txt");
  std::vector<base::FilePath> test_files;
  test_files.push_back(test_file1);
  test_files.push_back(test_file2);
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathsForTest(
      &test_files);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/open_multiple_existing"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenDirectoryTest) {
  base::FilePath test_directory =
      drive::util::GetDriveMountPointPath(browser()->profile()).AppendASCII(
          "root/subdir");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_directory);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/open_directory"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenDirectoryWithWriteTest) {
  base::FilePath test_directory =
      drive::util::GetDriveMountPointPath(browser()->profile()).AppendASCII(
          "root/subdir");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_directory);
  ASSERT_TRUE(
      RunPlatformAppTest("api_test/file_system/open_directory_with_write"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenDirectoryWithoutPermissionTest) {
  base::FilePath test_directory =
      drive::util::GetDriveMountPointPath(browser()->profile()).AppendASCII(
          "root/subdir");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_directory);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/open_directory_without_permission"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiOpenDirectoryWithOnlyWritePermissionTest) {
  base::FilePath test_directory =
      drive::util::GetDriveMountPointPath(browser()->profile()).AppendASCII(
          "root/subdir");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_directory);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/open_directory_with_only_write"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiSaveNewFileTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/save_new.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/save_new"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
                       FileSystemApiSaveExistingFileTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/save_existing.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/save_existing"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
    FileSystemApiSaveNewFileWithWriteTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/save_new.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest("api_test/file_system/save_new_with_write"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForDrive,
    FileSystemApiSaveExistingFileWithWriteTest) {
  base::FilePath test_file = drive::util::GetDriveMountPointPath(
      browser()->profile()).AppendASCII("root/save_existing.txt");
  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &test_file);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/save_existing_with_write")) << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem, Background) {
  EnterKioskSession();
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_OK);
  ASSERT_TRUE(
      RunPlatformAppTest("api_test/file_system/request_file_system_background"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem, ReadOnly) {
  EnterKioskSession();
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_OK);
  ASSERT_TRUE(
      RunPlatformAppTest("api_test/file_system/request_file_system_read_only"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem, Writable) {
  EnterKioskSession();
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_OK);
  ASSERT_TRUE(
      RunPlatformAppTest("api_test/file_system/request_file_system_writable"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem, UserReject) {
  EnterKioskSession();
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_CANCEL);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/request_file_system_user_reject"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem, NotKioskSession) {
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_OK);
  ASSERT_TRUE(RunPlatformAppTest(
      "api_test/file_system/request_file_system_not_kiosk_session"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem,
                       WhitelistedComponent) {
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_CANCEL);
  ASSERT_TRUE(RunPlatformAppTestWithFlags(
      "api_test/file_system/request_file_system_whitelisted_component",
      kFlagLoadAsComponent))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileSystemApiTestForRequestFileSystem,
                       NotWhitelistedComponent) {
  ScopedSkipRequestFileSystemDialog dialog_skipper(ui::DIALOG_BUTTON_OK);
  ASSERT_TRUE(RunPlatformAppTestWithFlags(
      "api_test/file_system/request_file_system_not_whitelisted_component",
      kFlagLoadAsComponent))
      << message_;
}

}  // namespace extensions
