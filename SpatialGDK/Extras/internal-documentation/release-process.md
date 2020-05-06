# GDK for Unreal Release Process

This document outlines the process for releasing a version of the GDK for Unreal and all associated projects.

> This process will need to be reviewed once we hit beta.

## Terminology
* **Release version** is the version of the SpatialOS GDK for Unreal that you are releasing by performing the steps in this document.
* **Previous version** is the latest version of the SpatialOS GDK for Unreal that is currently released to customers. You can find out what this version is [here](https://github.com/spatialos/UnrealGDK/releases).
* `<GameRoot>` - The directory that contains your project’s .uproject file and Source folder.
* `<ProjectRoot>` - The directory that contains your `<GameRoot>` directory.
* `<YourProject>` - The name of your project and .uproject file (for example, `\<GameRoot>\YourProject.uproject`).

## Validation pre-requisites

The following entry criteria must be met before you start the validation steps:

### Ensure Xbox DLLs exist

To check that Xbox-compatible Worker SDK DLLs are available.

1. Identify the Worker SDK version pinned by the GDK. To do this, check [core-sdk.version](https://github.com/spatialos/UnrealGDK/blob/master/SpatialGDK/Extras/core-sdk.version) on `master`.
1. Identify the XDK version(s) officially supported in all UE4 versions that the GDK version you're about to release supports. To do this:
    1. Google search for the release notes for all UE4 versions that the GDK version you're about to release supports (for example [4.22 Release Notes](https://docs.unrealengine.com/en-US/Support/Builds/ReleaseNotes/4_22/index.html))
    1. Search in page (ctrl-f) for `XDK` and note down the supported version (for example, the [4.22 Release Notes](https://docs.unrealengine.com/en-US/Support/Builds/ReleaseNotes/4_22/index.html) reveals that it supports `XDK: July 2018 QFE-4`).
    1. Convert `XDK: July 2018 QFE-4` to the format that `spatial package get` expects. This is **year** **month** **QFE Version**. For example `XDK: July 2018 QFE-4` converts to `180704`.
    1. Using the information you just ascertained, fill in the `<...>` blanks in the below command and run it via command line:<br>
`spatial package get worker_sdk c-dynamic-x86_64-<XDK version>-xbone <SDK version> c-sdk-<SDK version>.zip`<br>
A correct command looks something like this:<br>
`spatial package get worker_sdk c-dynamic-x86_64-xdk180401-xbone 13.7.1 c-sdk-13.7.1-180401.zip`<br>
If it succeeds it will download a DLL.<br>
If it fails because the DLL is not available, file a WRK ticket for the Worker team to generate the required DLL(s). See [WRK-1676](https://improbableio.atlassian.net/browse/WRK-1676) for an example.

### Create the `UnrealGDK` release candidate
1. Notify `#dev-unreal-internal` that you intend to commence a release. Ask if anyone `@here` knows of any blocking defects in code or docs that should be resolved prior to commencement of the release process.
1. `git clone` the [UnrealGDK](https://github.com/spatialos/UnrealGDK).
1. `git checkout master`
1. `git pull`
1. Using `git log`, take note of the latest commit hash.
1. `git checkout -b x.y.z-rc` in order to create release candidate branch.
1. Open `CHANGELOG.md`, which is in the root of the repository.
1. Read **every** release note in the `Unreleased` section. Ensure that they make sense, they conform to [how-to-write-good-release-notes.md](https://github.com/spatialos/UnrealGDK/blob/master/SpatialGDK/Extras/internal-documentation/how-to-write-good-release-notes.md) structure.
1. Compare `master` to `release` using the GitHub UI and ensure that every change that requires a release note has one.
1. Enter the release version and planned date of release in a `##` block. Move the `Unreleased` section above this.
    - Look at the previous release versions in the changelog to see how this should be done.
1. Commit your changes to `CHANGELOG.md`.
1. Open `SpatialGDK/SpatialGDK.uplugin`.
1. Increment the `VersionName` and `Version`.
1. Commit your changes to `SpatialGDK/SpatialGDK.uplugin`.
1. `git push --set-upstream origin x.y.z-rc` to push the branch.
1. Announce the branch and the commit hash it uses in the `#unreal-gdk-release` channel.

### Create the `improbableio/UnrealEngine` release candidate
1. `git clone` the [improbableio/UnrealEngine](https://github.com/improbableio/UnrealEngine).
1. `git checkout 4.xx-SpatialOSUnrealGDK`
1. `git pull`
1. Using `git log`, take note of the latest commit hash.
1. `git checkout -b 4.xx-SpatialOSUnrealGDK-x.y.z-rc` in order to create release candidate branch.
1. `git push --set-upstream origin 4.xx-SpatialOSUnrealGDK-x.y.z-rc` to push the branch.
1. Repeat the above steps for all supported `4.xx` engine versions.
1. Announce the branch and the commit hash it uses in the `#unreal-gdk-release` channel.
1. Make sure to update UnrealGDKExampleProjectVersion.txt and UnrealGDKVersion.txt so that they contain the relevant release tag for the UnrealGDK and UnrealGDKExampleProject.

### Create the `UnrealGDKExampleProject` release candidate
1. `git clone` the [UnrealGDKExampleProject](https://github.com/spatialos/UnrealGDKExampleProject).
1. `git checkout master`
1. `git pull`
1. Using `git log`, take note of the latest commit hash.
1. `git checkout -b x.y.z-rc` in order to create release candidate branch.
1. `git push --set-upstream origin x.y.z-rc` to push the branch.
1. Announce the branch and the commit hash it uses in the #unreal-gdk-release channel.

## Build your release candidate engine
1. Open https://documentation.improbable.io/gdk-for-unreal/docs/get-started-1-get-the-dependencies.
1. Uninstall all dependencies listed on this page so that you can accurately validate our installation steps.
1. If you have one, delete your local clone of `UnrealEngine`.
1. Follow the installation steps on https://documentation.improbable.io/gdk-for-unreal/docs/get-started-1-get-the-dependencies.
1. When you clone the `UnrealEngine`, be sure to checkout `x.y.z-rc-x` so you're building the release version.

## Implementing fixes

If at any point in the below validation steps you encounter a blocker, you must fix that defect prior to releasing.

The workflow for this is:

1. Raise a bug ticket in JIRA detailing the blocker.
1. `git checkout x.y.z-rc`
1. `git pull`
1. `git checkout -b bugfix/UNR-xxx`
1. Fix the defect.
1. `git commit`, `git push -u origin HEAD`, target your PR at `x.y.z-rc`.
1. When the PR is merged, `git checkout x.y.z-rc`, `git pull` and re-test the defect to ensure you fixed it.
1. Notify #unreal-gdk-release that the release candidate has been updated.
1. **Judgment call**: If the fix was isolated, continue the validation steps from where you left off. If the fix was significant, restart testing from scratch. Consult the rest of the team if you are unsure which to choose.

## Validation (GDK, Starter Template and Example Project)
You must perform these steps twice, once in the EU region and once in CN.

1. Open the [Component Release](https://improbabletest.testrail.io/index.php?/suites/view/72) test suite and click run test.
1. Name your test run in this format: Component release: GDK [UnrealGDK version], UE (Unreal Engine version), [region].
1. Execute the test runs.

## Validation (Docs)
1. @techwriters in [#docs](https://improbable.slack.com/archives/C0TBQAB5X) and ask them what's changes in the docs since the last release.
1. Proof read the pages that have changed.
1. Spend an additional 20 minutes reading the docs and ensuring that nothing is incorrect.

## Release

All of the above tests **must** have passed and there must be no outstanding blocking issues before you start this, the release phase.

The order of `git merge` operations in all UnrealGDK related repositories is:<br>
`release candidate` > `preview` > `release` > `master`

If you want to soak test this release on the `preview` branch before promoting it to the `release` branch, only execute the steps that merge into `preview` and `master`.

1. When merging the following PRs, you need to enable `Allow merge commits` option on the repos and choose `Create a merge commit` from the dropdown in the pull request UI to merge the branch, then disable `Allow merge commits` option on the repos once the release process is complete. You need to be an admin to perform this.

**UnrealGDK**
1. In `UnrealGDK`, merge `x.y.z-rc` into `preview`.
1. If you want to soak test this release on the `preview`, use the [GitHub Release UI](https://github.com/spatialos/UnrealGDK/releases) to tag the commit you just made to `preview` as `x.y.z-preview`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `UnrealGDK`, merge `preview` into `release`.
1. Use the [GitHub Release UI](https://github.com/spatialos/UnrealGDK/releases) to tag the commit you just made to `release` as `x.y.z`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `UnrealGDK`, merge `release` into `master`. This merge could have conflicts. Don't hesitate to ask for help resolving these if you are unsure.

**improbableio/UnrealEngine**
1. In `improbableio/UnrealEngine`, merge `4.xx-SpatialOSUnrealGDK-x.y.z-rc` into `4.xx-SpatialOSUnrealGDK-preview`.
1. If you want to soak test this release on the `preview`, use the [GitHub Release UI](https://github.com/improbableio/UnrealEngine/releases) to tag the commit you just made to `4.xx-SpatialOSUnrealGDK-preview` as `4.xx-SpatialOSUnrealGDK-x.y.z-preview`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `improbableio/UnrealEngine`, merge `4.xx-SpatialOSUnrealGDK-preview` into `4.xx-SpatialOSUnrealGDK-release`.
1. Use the [GitHub Release UI](https://github.com/improbableio/UnrealEngine/releases) to tag the commit you just made to `release` as `4.xx-SpatialOSUnrealGDK-x.y`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `improbableio/UnrealEngine`, merge `4.xx-SpatialOSUnrealGDK-release` into `master`. This merge could have conflicts. Don't hesitate to ask for help resolving these if you are unsure.

**UnrealGDKExampleProject**
1. In `UnrealGDKExampleProject`, merge `x.y.z-rc` into `preview`.
1. If you want to soak test this release on the `preview`, use the [GitHub Release UI](https://github.com/spatialos/UnrealGDKExampleProject/releases) to tag the commit you just made to `preview` as `x.y.z-preview`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `UnrealGDKExampleProject`, merge `preview` into `release`.
1. Use the [GitHub Release UI](https://github.com/spatialos/UnrealGDKExampleProject/releases) to tag the commit you just made to `release` as `x.y.z`.<br/>
Copy the latest release notes from `CHANGELOG.md` and paste them into the release description field.
1. In `UnrealGDK`, merge `release` into `master`.

**Documentation**
1. Notify @techwriters in [#docs](https://improbable.slack.com/archives/C0TBQAB5X) that they may publish the new version of the docs.

**Announce**
1. Announce the release in:

* Forums
* Discord (`#unreal`, do not `@here`)
* Slack (`#releases`)
* Email (`unreal-interest@`)

Congratulations, you've completed the release process!

## Clean up

1. Delete all `rc` branches.

## Appendix

<details>
  <summary>Forum Post Template</summary>

 We are happy to announce the release of version x.y.z of the SpatialOS GDK for Unreal.

Please see the full release notes on GitHub:

Unreal GDK - https://github.com/spatialos/UnrealGDK/releases/tag/x.y.z<br/>
Corresponding fork of Unreal Engine - https://github.com/improbableio/UnrealEngine/releases/tag/4.xx-SpatialOSUnrealGDK-x.y.z<br/>
Corresponding version of the Example Project - https://github.com/spatialos/UnrealGDKExampleProject/releases/tag/x.y.z <br/>

</details>
