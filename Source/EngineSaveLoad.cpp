#include "Core.hpp"

void SaveGame()
{
    wstring TFlags;
    for (int i = 0; i <= 99; i++)
    {
        if (ToggleFlags[i].Value == 1)
            TFlags += L'1';
        else
            TFlags += L'0';
    }
    TFlags += L':';
    for (int i = 0; i < BACKPACK_SLOTS; i++)
    {
        TFlags += to_wstring(gInventory.backpack[i].itemId) + L',' + to_wstring(gInventory.backpack[i].quantity) + L';';
    }
    TFlags += L':' + to_wstring(gInventory.coins);

    wofstream Outfile;
    Outfile.open("GameData/Saves/TF.sav");
    Outfile << TFlags;

    wstring Position = to_wstring(OmegaTechData.MainCamera.position.x) + L':' +
                       to_wstring(OmegaTechData.MainCamera.position.y) + L':' +
                       to_wstring(OmegaTechData.MainCamera.position.z) + L':' +
                       to_wstring(OmegaTechData.LevelIndex) + L':';

    wofstream Outfile1;
    Outfile1.open("GameData/Saves/POS.sav");
    Outfile1 << Position;

    wofstream Outfile2;
    Outfile2.open("GameData/Saves/Script.sav");
    Outfile2 << ExtraWDLInstructions;
}

void LoadSave()
{
    wstring TFlags = LoadFile("GameData/Saves/TF.sav");
    size_t tfLen = TFlags.size();

    for (int i = 0; i <= 99 && i < (int)tfLen; i++)
    {
        if (TFlags[i] == L'1')
            ToggleFlags[i].Value = 1;
        if (TFlags[i] == L'0')
            ToggleFlags[i].Value = 0;
    }

    size_t emSectionStart = TFlags.find(L':', 100);
    size_t emSectionEnd = string::npos;
    if (emSectionStart != string::npos && emSectionStart + 1 < TFlags.size()) {
        emSectionEnd = TFlags.find(L':', emSectionStart + 1);
        if (emSectionEnd != string::npos) {
            std::string emState(TFlags.begin() + emSectionStart + 1, TFlags.begin() + emSectionEnd);
            LightningEntityManager::Instance().DeserializeState(emState);
        }
    }

    size_t bpStart = emSectionEnd;
    if (bpStart != string::npos && bpStart + 1 < TFlags.size())
    {
        wstring bpData = TFlags.substr(bpStart + 1);
        size_t secondColon = bpData.find(L':');
        wstring slotData = (secondColon != string::npos) ? bpData.substr(0, secondColon) : bpData;
        size_t pos = 0;
        int slotIdx = 0;
        while (pos < slotData.size() && slotIdx < BACKPACK_SLOTS)
        {
            size_t semi = slotData.find(L';', pos);
            if (semi == string::npos)
                break;
            wstring pair = slotData.substr(pos, semi - pos);
            size_t comma = pair.find(L',');
            if (comma != string::npos)
            {
                try {
                    int id = stoi(pair.substr(0, comma));
                    int qty = stoi(pair.substr(comma + 1));
                    gInventory.backpack[slotIdx].itemId = id;
                    gInventory.backpack[slotIdx].quantity = qty;
                } catch (...) { }
            }
            pos = semi + 1;
            slotIdx++;
        }
        if (secondColon != string::npos)
        {
            wstring coinStr = bpData.substr(secondColon + 1);
            if (!coinStr.empty()) {
                try { gInventory.coins = stoi(coinStr); } catch (...) { }
            }
        }
    }

    wstring Position = LoadFile("GameData/Saves/POS.sav");

    if (!Position.empty())
    {
        try {
            OmegaTechData.LevelIndex = int(ToFloat(WSplitValue(Position, 3)));
        } catch (...) {
            OmegaTechData.LevelIndex = 1;
        }

        SetCameraFlag = true;

        try {
            int X = ToFloat(WSplitValue(Position, 0));
            int Y = ToFloat(WSplitValue(Position, 1));
            int Z = ToFloat(WSplitValue(Position, 2));
            SetCameraPos = {float(X), float(Y), float(Z)};
        } catch (...) {
            SetCameraPos = {0.0f, 10.0f, 0.0f};
        }
    }

    ExtraWDLInstructions = LoadFile("GameData/Saves/Script.sav");
}
