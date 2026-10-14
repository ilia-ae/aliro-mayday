#pragma once
class NfcManager;
namespace aliro::nfc {
void refreshReader(NfcManager* manager);
void invalidateReader(NfcManager* manager);
}
