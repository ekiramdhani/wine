/*
 * WINE Platform native bus driver
 *
 * Copyright 2016 Aric Stewart
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define NONAMELESSUNION

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winternl.h"
#include "winreg.h"
#include "setupapi.h"
#include "ddk/wdm.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/list.h"

#include "bus.h"

WINE_DEFAULT_DEBUG_CHANNEL(plugplay);

struct pnp_device
{
    struct list entry;
    DEVICE_OBJECT *device;
};

struct device_extension
{
    void *native;  /* Must be the first member of the structure */

    WORD vid, pid;
    DWORD uid, version, index;
    BOOL is_gamepad;
    WCHAR *serial;
    const WCHAR *busid;  /* Expected to be a static constant */
};

static CRITICAL_SECTION device_list_cs;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &device_list_cs,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": device_list_cs") }
};
static CRITICAL_SECTION device_list_cs = { &critsect_debug, -1, 0, 0, 0, 0 };

static struct list pnp_devset = LIST_INIT(pnp_devset);

static const WCHAR zero_serialW[]= {'0','0','0','0',0};
static const WCHAR imW[] = {'I','M',0};
static const WCHAR igW[] = {'I','G',0};

static inline WCHAR *strdupW(const WCHAR *src)
{
    WCHAR *dst;
    if (!src) return NULL;
    dst = HeapAlloc(GetProcessHeap(), 0, (strlenW(src) + 1)*sizeof(WCHAR));
    if (dst) strcpyW(dst, src);
    return dst;
}

static DWORD get_vidpid_index(WORD vid, WORD pid)
{
    struct pnp_device *ptr;
    DWORD index = 1;

    LIST_FOR_EACH_ENTRY(ptr, &pnp_devset, struct pnp_device, entry)
    {
        struct device_extension *ext = (struct device_extension *)ptr->device->DeviceExtension;
        if (ext->vid == vid && ext->pid == pid)
            index = max(ext->index + 1, index);
    }

    return index;
}

static WCHAR *get_instance_id(DEVICE_OBJECT *device)
{
    static const WCHAR formatW[] =  {'%','s','\\','V','i','d','_','%','0','4','x','&', 'P','i','d','_','%','0','4','x','&',
                                     '%','s','_','%','i','\\','%','i','&','%','s','&','%','x',0};
    struct device_extension *ext = (struct device_extension *)device->DeviceExtension;
    const WCHAR *serial = ext->serial ? ext->serial : zero_serialW;
    DWORD len = strlenW(ext->busid) + strlenW(serial) + 64;
    WCHAR *dst;

    if ((dst = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR))))
        sprintfW(dst, formatW, ext->busid, ext->vid, ext->pid, ext->is_gamepad ? igW : imW,
                 ext->index, ext->version, serial, ext->uid);

    return dst;
}

static WCHAR *get_device_id(DEVICE_OBJECT *device)
{
    static const WCHAR formatW[] =  {'%','s','\\','V','i','d','_','%','0','4','x','&','P','i','d','_','%','0','4','x',0};
    struct device_extension *ext = (struct device_extension *)device->DeviceExtension;
    DWORD len = strlenW(ext->busid) + 19;
    WCHAR *dst;

    if ((dst = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR))))
        sprintfW(dst, formatW, ext->busid, ext->vid, ext->pid);

    return dst;
}

static WCHAR *get_compatible_ids(DEVICE_OBJECT *device)
{
    struct device_extension *ext = (struct device_extension *)device->DeviceExtension;
    WCHAR *iid, *did, *dst, *ptr;
    DWORD len;

    if (!(iid = get_instance_id(device)))
        return NULL;

    if (!(did = get_device_id(device)))
    {
        HeapFree(GetProcessHeap(), 0, iid);
        return NULL;
    }

    len = strlenW(iid) + strlenW(did) + strlenW(ext->busid) + 4;
    if ((dst = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR))))
    {
        ptr = dst;
        strcpyW(ptr, iid);
        ptr += strlenW(iid) + 1;
        strcpyW(ptr, did);
        ptr += strlenW(did) + 1;
        strcpyW(ptr, ext->busid);
        ptr += strlenW(ext->busid) + 1;
        *ptr = 0;
    }

    HeapFree(GetProcessHeap(), 0, iid);
    HeapFree(GetProcessHeap(), 0, did);
    return dst;
}

DEVICE_OBJECT *bus_create_hid_device(DRIVER_OBJECT *driver, const WCHAR *busidW, void *native, WORD vid,
                                     WORD pid, DWORD version, DWORD uid, const WCHAR *serialW, BOOL is_gamepad,
                                     const GUID *class)
{
    static const WCHAR device_name_fmtW[] = {'\\','D','e','v','i','c','e','\\','%','s','#','%','p',0};
    struct device_extension *ext;
    struct pnp_device *pnp_dev;
    DEVICE_OBJECT *device;
    UNICODE_STRING nameW;
    WCHAR dev_name[256];
    HDEVINFO devinfo;
    NTSTATUS status;

    TRACE("(%p, %s, %p, %04x, %04x, %u, %u, %s, %u, %s)\n", driver, debugstr_w(busidW), native,
          vid, pid, version, uid, debugstr_w(serialW), is_gamepad, debugstr_guid(class));

    if (!(pnp_dev = HeapAlloc(GetProcessHeap(), 0, sizeof(*pnp_dev))))
        return NULL;

    sprintfW(dev_name, device_name_fmtW, busidW, native);
    RtlInitUnicodeString(&nameW, dev_name);
    status = IoCreateDevice(driver, sizeof(*ext), &nameW, 0, 0, FALSE, &device);
    if (status)
    {
        FIXME("failed to create device error %x\n", status);
        HeapFree(GetProcessHeap(), 0, pnp_dev);
        return NULL;
    }

    EnterCriticalSection(&device_list_cs);

    /* fill out device_extension struct */
    ext = (struct device_extension *)device->DeviceExtension;
    ext->native     = native;
    ext->vid        = vid;
    ext->pid        = pid;
    ext->uid        = uid;
    ext->version    = version;
    ext->index      = get_vidpid_index(vid, pid);
    ext->is_gamepad = is_gamepad;
    ext->serial     = strdupW(serialW);
    ext->busid      = busidW;

    /* add to list of pnp devices */
    pnp_dev->device = device;
    list_add_tail(&pnp_devset, &pnp_dev->entry);

    LeaveCriticalSection(&device_list_cs);

    devinfo = SetupDiGetClassDevsW(class, NULL, NULL, DIGCF_DEVICEINTERFACE);
    if (devinfo)
    {
        SP_DEVINFO_DATA data;
        WCHAR *instance;

        data.cbSize = sizeof(data);
        if (!(instance = get_instance_id(device)))
            ERR("failed to generate instance id\n");
        else if (!SetupDiCreateDeviceInfoW(devinfo, instance, class, NULL, NULL, DICD_INHERIT_CLASSDRVS, &data))
            ERR("failed to create device info: %x\n", GetLastError());
        else if (!SetupDiRegisterDeviceInfo(devinfo, &data, 0, NULL, NULL, NULL))
            ERR("failed to register device info: %x\n", GetLastError());

        HeapFree(GetProcessHeap(), 0, instance);
        SetupDiDestroyDeviceInfoList(devinfo);
    }
    else
        ERR("failed to get ClassDevs: %x\n", GetLastError());

    IoInvalidateDeviceRelations(device, BusRelations);
    return device;
}

static NTSTATUS handle_IRP_MN_QUERY_ID(DEVICE_OBJECT *device, IRP *irp)
{
    NTSTATUS status = irp->IoStatus.u.Status;
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );
    BUS_QUERY_ID_TYPE type = irpsp->Parameters.QueryId.IdType;

    TRACE("(%p, %p)\n", device, irp);

    switch (type)
    {
        case BusQueryHardwareIDs:
            TRACE("BusQueryHardwareIDs\n");
            irp->IoStatus.Information = (ULONG_PTR)get_compatible_ids(device);
            break;
        case BusQueryCompatibleIDs:
            TRACE("BusQueryCompatibleIDs\n");
            irp->IoStatus.Information = (ULONG_PTR)get_compatible_ids(device);
            break;
        case BusQueryDeviceID:
            TRACE("BusQueryDeviceID\n");
            irp->IoStatus.Information = (ULONG_PTR)get_device_id(device);
            break;
        case BusQueryInstanceID:
            TRACE("BusQueryInstanceID\n");
            irp->IoStatus.Information = (ULONG_PTR)get_instance_id(device);
            break;
        default:
            FIXME("Unhandled type %08x\n", type);
            return status;
    }

    status = irp->IoStatus.Information ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    return status;
}

NTSTATUS WINAPI common_pnp_dispatch(DEVICE_OBJECT *device, IRP *irp)
{
    NTSTATUS status = irp->IoStatus.u.Status;
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation(irp);

    switch (irpsp->MinorFunction)
    {
        case IRP_MN_QUERY_DEVICE_RELATIONS:
            TRACE("IRP_MN_QUERY_DEVICE_RELATIONS\n");
            break;
        case IRP_MN_QUERY_ID:
            TRACE("IRP_MN_QUERY_ID\n");
            status = handle_IRP_MN_QUERY_ID(device, irp);
            irp->IoStatus.u.Status = status;
            break;
        case IRP_MN_QUERY_CAPABILITIES:
            TRACE("IRP_MN_QUERY_CAPABILITIES\n");
            break;
        default:
            FIXME("Unhandled function %08x\n", irpsp->MinorFunction);
            break;
    }

    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS WINAPI DriverEntry( DRIVER_OBJECT *driver, UNICODE_STRING *path )
{
    static const WCHAR udevW[] = {'\\','D','r','i','v','e','r','\\','U','D','E','V',0};
    static UNICODE_STRING udev = {sizeof(udevW) - sizeof(WCHAR), sizeof(udevW), (WCHAR *)udevW};

    TRACE( "(%p, %s)\n", driver, debugstr_w(path->Buffer) );

    IoCreateDriver(&udev, udev_driver_init);

    return STATUS_SUCCESS;
}
