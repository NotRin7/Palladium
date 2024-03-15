Palladium Coin Core Integration/Staging Tree
https://palladiumcoin.com

What is Palladium Coin?
Palladium Coin is an innovative digital currency designed for instant transactions globally. Operating without a central authority, Palladium Coin utilizes peer-to-peer technology for transaction management and currency issuance. Palladium Coin Core serves as the foundational open-source software facilitating the use of this cryptocurrency.

For further details and to access the binary version of Palladium Coin Core software, visit https://palladiumcoin.com/en/download/, or refer to the original whitepaper.

License
Palladium Coin Core is released under the MIT license terms. Refer to COPYING for comprehensive details or visit https://opensource.org/licenses/MIT.

Development Process
While the master branch undergoes regular builds and testing, stability cannot be guaranteed. Periodic tags signify new official and stable releases of Palladium Coin Core.

The contribution process is outlined in CONTRIBUTING.md, while developers can find valuable insights in doc/developer-notes.md.

Testing
Testing and code review constitute critical aspects of development. We encourage community participation in testing pull requests to ensure robustness and security.

Automated Testing
Developers are urged to incorporate unit tests for new code and enhance existing code coverage. Unit tests can be executed with: make check (assuming they're enabled in configure). Detailed instructions on running and expanding unit tests are available in /src/test/README.md.

Regression and integration tests, scripted in Python, are automatically executed on the build server. To run these tests (after installing test dependencies): test/functional/test_runner.py

Travis CI ensures each pull request undergoes builds for Windows, Linux, and macOS, with automatic execution of unit/sanity tests.

Manual Quality Assurance (QA) Testing
It's essential that changes undergo testing by individuals other than the code author, especially for significant or high-risk alterations. If testing isn't straightforward, it's advisable to include a test plan in the pull request description.

Translations
Translations and updates can be submitted via Palladium Coin Core's Transifex page.

Translations are periodically merged into the git repository from Transifex. Refer to the translation process for insights into this workflow.

Note: Translation changes shouldn't be submitted as GitHub pull requests to avoid automatic overwriting during the next Transifex pull. Translators are encouraged to subscribe to the mailing list for updates.
