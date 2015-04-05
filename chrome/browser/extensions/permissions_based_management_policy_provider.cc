// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/permissions_based_management_policy_provider.h"

#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_management.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/permissions/permission_set.h"
#include "grit/extensions_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

PermissionsBasedManagementPolicyProvider::
    PermissionsBasedManagementPolicyProvider(ExtensionManagement* settings)
    : settings_(settings) {
}

PermissionsBasedManagementPolicyProvider::
    ~PermissionsBasedManagementPolicyProvider() {
}

std::string
PermissionsBasedManagementPolicyProvider::GetDebugPolicyProviderName() const {
#ifdef NDEBUG
  NOTREACHED();
  return std::string();
#else
  return "Controlled by enterprise policy, restricting extension permissions.";
#endif
}

bool PermissionsBasedManagementPolicyProvider::UserMayLoad(
    const Extension* extension,
    base::string16* error) const {
  // Component extensions are always allowed.
  if (Manifest::IsComponentLocation(extension->location()))
    return true;

  scoped_refptr<const PermissionSet> required_permissions =
      PermissionsParser::GetRequiredPermissions(extension);

  if (!settings_->IsPermissionSetAllowed(extension, required_permissions)) {
    if (error) {
      *error =
          l10n_util::GetStringFUTF16(IDS_EXTENSION_CANT_INSTALL_POLICY_BLOCKED,
                                     base::UTF8ToUTF16(extension->name()),
                                     base::UTF8ToUTF16(extension->id()));
    }
    return false;
  }

  return true;
}

}  // namespace extensions