# Contributing

[Русский](CONTRIBUTING.ru.md) · [License v1.1](LICENSE) · [Licensing guide](docs/licensing.md) · [Authorship](AUTHORS.md)

Changes to project-original material use the Source-Available Commercial License
v1.1. Changes to independently licensed components keep their applicable license,
including its attribution, change notices and other requirements.
The applicable license text governs; this guide explains the submission process.

## Copyright and the incoming grant

You retain copyright in your own contribution. For a new contribution to
project-original code or documentation, explicitly accept **LICENSE §6, v1.1**.
That grants Ilia Arestov, or a successor to the project's licensing rights, a
non-exclusive, irrevocable, worldwide copyright license without time limits or
royalties. It permits use, adaptation, distribution and sublicensing in both the
public source-available project and its separate paid commercial releases.
Payment or support obligations require a separate written agreement.

You remain free to use or license material you own elsewhere. The grant covers
only rights you can grant; it does not assign copyright or change anyone else's
component license. It gives public recipients no free commercial-use permission
for the covered project code.

## Submitting a change

1. Describe the change and relevant validation in the pull request.
2. Identify third-party material, its source, license and known restrictions.
   Preserve notices. Obtain employer/other rights-holder permission where needed.
3. In the PR's licensing section, explicitly confirm the following statement for
   the material in that submission:

   > I have the rights or authorization needed for this submission. For its
   > project-original contributions, I expressly accept section 6 of the
   > aliro.mayday Source-Available Commercial License v1.1, including the Project
   > Owner's right to include them in paid commercial releases. Independently
   > licensed material keeps its applicable license and is identified in this PR.

4. Use the [PR template](.github/PULL_REQUEST_TEMPLATE.md), or provide the same
   express written confirmation in a linked record identifying this submission
   and license version. A license badge, issue discussion or unconfirmed checkbox
   does not replace that confirmation. Maintainers must establish the rights and
   retain the acceptance record before merging.

If a change affects only independently licensed files, state their licenses and
that there is no project-original contribution. No additional §6 grant is required
for those files. The new incoming grant is prospective: existing submissions keep
their previous terms unless their contributor separately agrees in writing.

Keep credentials, controller backups, private logs and personal documents out of
public PRs. Relevant build/test commands are in the [firmware guide](aliro-esp32/README.md)
and [test guide](aliro-esp32/tests/README.md). Include updated source manifest hashes
when changing tracked release files.
