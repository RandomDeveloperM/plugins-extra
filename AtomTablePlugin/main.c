/*
 * Process Hacker Extra Plugins -
 *   NT Atom Table Plugin
 *
 * Copyright (C) 2015 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#define CINTERFACE
#define COBJMACROS
#include <phdk.h>
#include <phappresource.h>
#include <settings.h>
#include "resource.h"

#define ATOM_TABLE_MENUITEM 1000
#define PLUGIN_NAME L"dmex.AtomTablePlugin"
#define SETTING_NAME_WINDOW_POSITION (PLUGIN_NAME L".WindowPosition")
#define SETTING_NAME_WINDOW_SIZE (PLUGIN_NAME L".WindowSize")
#define SETTING_NAME_LISTVIEW_COLUMNS (PLUGIN_NAME L".ListViewColumns")

static PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
static PH_CALLBACK_REGISTRATION MainMenuInitializingCallbackRegistration;
static PH_CALLBACK_REGISTRATION PluginShowOptionsCallbackRegistration;
static HWND ListViewWndHandle;
static PH_LAYOUT_MANAGER LayoutManager;
static PPH_PLUGIN PluginInstance;

NTSTATUS EnumAtomTable(
    _Out_ PATOM_TABLE_INFORMATION* AtomTable
    )
{
    NTSTATUS status;
    PVOID buffer;
    ULONG bufferSize = 0x1000;

    buffer = PhAllocate(bufferSize);
    memset(buffer, 0, bufferSize);

    status = NtQueryInformationAtom(
        RTL_ATOM_INVALID_ATOM,
        AtomTableInformation,
        buffer,
        bufferSize,
        &bufferSize // Not used...
        );

    if (!NT_SUCCESS(status))
    {
        PhFree(buffer);
        return status;
    }

    *AtomTable = buffer;

    return status;
}

NTSTATUS QueryAtomTableEntry(
    _In_ RTL_ATOM Atom,
    _Out_ PATOM_BASIC_INFORMATION* AtomInfo
    )
{
    NTSTATUS status;
    PVOID buffer;
    ULONG bufferSize = 0x1000;

    buffer = PhAllocate(bufferSize);
    memset(buffer, 0, bufferSize);

    status = NtQueryInformationAtom(
        Atom,
        AtomBasicInformation,
        buffer,
        bufferSize,
        &bufferSize // Not used...
        );

    if (!NT_SUCCESS(status))
    {
        PhFree(buffer);
        return status;
    }

    *AtomInfo = buffer;

    return status;
}

VOID LoadAtomTable(VOID)
{
    PATOM_TABLE_INFORMATION atomTable = NULL;

    if (!NT_SUCCESS(EnumAtomTable(&atomTable)))
        return;

    ExtendedListView_SetRedraw(ListViewWndHandle, FALSE);
    ListView_DeleteAllItems(ListViewWndHandle);

    for (ULONG i = 0; i < atomTable->NumberOfAtoms; i++)
    {
        PATOM_BASIC_INFORMATION atomInfo = NULL;

        if (!NT_SUCCESS(QueryAtomTableEntry(atomTable->Atoms[i], &atomInfo)))
        {
            PhAddListViewItem(ListViewWndHandle, MAXINT, PhaFormatString(L"(Error) #%lu", i)->Buffer, NULL);
            continue;
        }

        if ((atomInfo->Flags & RTL_ATOM_PINNED) == RTL_ATOM_PINNED)
        {
            INT index = PhAddListViewItem(
                ListViewWndHandle,
                MAXINT,
                PhaFormatString(L"%s (Pinned)", atomInfo->Name)->Buffer,
                NULL
                );
            PhSetListViewSubItem(
                ListViewWndHandle,
                index,
                1,
                PhaFormatString(L"%u", atomInfo->UsageCount)->Buffer
                );
        }
        else
        {
            INT index = PhAddListViewItem(
                ListViewWndHandle,
                MAXINT,
                atomInfo->Name,
                NULL
                );
            PhSetListViewSubItem(
                ListViewWndHandle,
                index,
                1,
                PhaFormatString(L"%u", atomInfo->UsageCount)->Buffer
                );
        }

        PhFree(atomInfo);
    }

    ExtendedListView_SetRedraw(ListViewWndHandle, TRUE);

    PhFree(atomTable);
}

PPH_STRING PhGetSelectedListViewItemText(
    _In_ HWND hWnd
    )
{
    INT index = PhFindListViewItemByFlags(
        hWnd,
        -1,
        LVNI_SELECTED
        );

    if (index != -1)
    {
        WCHAR buffer[DOS_MAX_PATH_LENGTH] = L"";

        LVITEM item;
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.iSubItem = 0;
        item.pszText = buffer;
        item.cchTextMax = ARRAYSIZE(buffer);

        if (ListView_GetItem(hWnd, &item))
            return PhCreateString(buffer);
    }

    return NULL;
}

VOID ShowStatusMenu(
    _In_ HWND hwndDlg
    )
{
    PPH_STRING cacheEntryName;
        
    cacheEntryName = PhGetSelectedListViewItemText(ListViewWndHandle);

    if (cacheEntryName)
    {
        POINT cursorPos;
        PPH_EMENU menu;
        PPH_EMENU_ITEM selectedItem;

        GetCursorPos(&cursorPos);

        menu = PhCreateEMenu();
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"Remove", NULL, NULL), -1);

        selectedItem = PhShowEMenu(
            menu,
            ListViewWndHandle,
            PH_EMENU_SHOW_LEFTRIGHT,
            PH_ALIGN_LEFT | PH_ALIGN_TOP,
            cursorPos.x,
            cursorPos.y
            );

        if (selectedItem && selectedItem->Id != -1)
        {
            switch (selectedItem->Id)
            {
            case 1:
                {
                    INT lvItemIndex = PhFindListViewItemByFlags(
                        ListViewWndHandle,
                        -1,
                        LVNI_SELECTED
                        );

                    if (lvItemIndex != -1)
                    {
                        if (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
                            hwndDlg,
                            L"remove",
                            cacheEntryName->Buffer,
                            NULL,
                            FALSE
                            ))
                        {
                            PATOM_TABLE_INFORMATION atomTable = NULL;

                            if (!NT_SUCCESS(EnumAtomTable(&atomTable)))
                                return;

                            for (ULONG i = 0; i < atomTable->NumberOfAtoms; i++)
                            {
                                PATOM_BASIC_INFORMATION atomInfo = NULL;

                                if (!NT_SUCCESS(QueryAtomTableEntry(atomTable->Atoms[i], &atomInfo)))
                                    continue;

                                if (!PhEqualStringZ(atomInfo->Name, cacheEntryName->Buffer, TRUE))
                                    continue;

                                do
                                {
                                    if (!NT_SUCCESS(NtDeleteAtom(atomTable->Atoms[i])))
                                    {
                                        break;
                                    }

                                    PhFree(atomInfo);
                                    atomInfo = NULL;

                                    if (!NT_SUCCESS(QueryAtomTableEntry(atomTable->Atoms[i], &atomInfo)))
                                        break;

                                } while (atomInfo->UsageCount >= 1);

                                ListView_DeleteItem(ListViewWndHandle, lvItemIndex);

                                if (atomInfo)
                                {
                                    PhFree(atomInfo);
                                }
                            }

                            PhFree(atomTable);
                        }
                    }
                }
                break;
            }
        }

        PhDestroyEMenu(menu);
        PhDereferenceObject(cacheEntryName);
    }
}

INT_PTR CALLBACK MainWindowDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PhCenterWindow(hwndDlg, PhMainWndHandle);
            ListViewWndHandle = GetDlgItem(hwndDlg, IDC_ATOMLIST);

            PhInitializeLayoutManager(&LayoutManager, hwndDlg);
            PhAddLayoutItem(&LayoutManager, ListViewWndHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDRETRY), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            PhRegisterDialog(hwndDlg);
            PhLoadWindowPlacementFromSetting(SETTING_NAME_WINDOW_POSITION, SETTING_NAME_WINDOW_SIZE, hwndDlg);

            PhSetListViewStyle(ListViewWndHandle, FALSE, TRUE);
            PhSetControlTheme(ListViewWndHandle, L"explorer");
            PhAddListViewColumn(ListViewWndHandle, 0, 0, 0, LVCFMT_LEFT, 370, L"Atom Name");
            PhAddListViewColumn(ListViewWndHandle, 1, 1, 1, LVCFMT_LEFT, 70, L"Ref Count");
            PhSetExtendedListView(ListViewWndHandle);
            PhLoadListViewColumnsFromSetting(SETTING_NAME_LISTVIEW_COLUMNS, ListViewWndHandle);

            LoadAtomTable();
        }
        break;
    case WM_SIZE:
        PhLayoutManagerLayout(&LayoutManager);
        break;
    case WM_DESTROY:
        PhSaveWindowPlacementToSetting(SETTING_NAME_WINDOW_POSITION, SETTING_NAME_WINDOW_SIZE, hwndDlg);
        PhSaveListViewColumnsToSetting(SETTING_NAME_LISTVIEW_COLUMNS, ListViewWndHandle);
        PhDeleteLayoutManager(&LayoutManager);
        PhUnregisterDialog(hwndDlg);
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
                break;
            case IDRETRY:
                LoadAtomTable();
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR hdr = (LPNMHDR)lParam;

            switch (hdr->code)
            {
            case NM_RCLICK:
                {
                    if (hdr->hwndFrom == ListViewWndHandle)
                        ShowStatusMenu(hwndDlg);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

VOID NTAPI MainMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;
    PPH_EMENU_ITEM systemMenu;

    if (menuInfo->u.MainMenu.SubMenuIndex != PH_MENU_ITEM_LOCATION_TOOLS)
        return;

    if (!(systemMenu = PhFindEMenuItem(menuInfo->Menu, 0, L"System", 0)))
    {
        PhInsertEMenuItem(menuInfo->Menu, systemMenu = PhPluginCreateEMenuItem(PluginInstance, 0, 0, L"&System", NULL), -1);
    }

    PhInsertEMenuItem(systemMenu, PhPluginCreateEMenuItem(PluginInstance, 0, ATOM_TABLE_MENUITEM, L"&Atom Table", NULL), -1);
}

VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = (PPH_PLUGIN_MENU_ITEM)Parameter;

    switch (menuItem->Id)
    {
    case ATOM_TABLE_MENUITEM:
        {
            DialogBox(
                PluginInstance->DllBase,
                MAKEINTRESOURCE(IDD_ATOMDIALOG),
                NULL,
                MainWindowDlgProc
                );
        }
        break;
    }
}

LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        {
            PPH_PLUGIN_INFORMATION info;
            PH_SETTING_CREATE settings[] =
            {
                { IntegerPairSettingType, SETTING_NAME_WINDOW_POSITION, L"350,350" },
                { ScalableIntegerPairSettingType, SETTING_NAME_WINDOW_SIZE, L"@96|510,380" },
                { StringSettingType, SETTING_NAME_LISTVIEW_COLUMNS, L"" }
            };

            PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);

            if (!PluginInstance)
                return FALSE;

            info->Author = L"dmex";
            info->DisplayName = L"Global Atom Table";
            info->Description = L"Plugin for viewing the Global Atom Table via the Tools menu.";
            info->HasOptions = FALSE;

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMainMenuInitializing),
                MainMenuInitializingCallback,
                NULL,
                &MainMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
                MenuItemCallback,
                NULL,
                &PluginMenuItemCallbackRegistration
                );

            PhAddSettings(settings, ARRAYSIZE(settings));
        }
        break;
    }

    return TRUE;
}
