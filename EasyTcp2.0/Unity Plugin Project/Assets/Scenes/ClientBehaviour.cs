using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.Runtime.InteropServices;
using System;
using AOT;

public enum NetCMD
{
    CMD_LOGIN,
    CMD_LOGIN_RESULT,
    CMD_LOGOUT,
    CMD_LOGOUT_RESULT,
    CMD_NEW_USER_JOIN,
    CMD_C2S_HEART,
    CMD_S2C_HEART,
    CMD_ERROR
};

public class ClientBehaviour : MonoBehaviour
{
    [DllImport("EasyTcpClientPulgin")]
    public static extern int Add(int a, int b);
    [DllImport("EasyTcpClientPulgin")]
    public static extern int Sub(int a, int b);

    // 消息回调
    public delegate void OnNetMsgCallBack(IntPtr csObj, IntPtr data, int len);

    [MonoPInvokeCallback(typeof(OnNetMsgCallBack))]
    public static void OnNetMsgCallBack1(IntPtr csObj, IntPtr data, int len)
    {
        // 将C++传入的对象指针还原为C#对象
        Debug.Log("OnNetMsgCallBack1.len=" + len);
        GCHandle h = GCHandle.FromIntPtr(csObj);
        ClientBehaviour obj = h.Target as ClientBehaviour;
        if(obj)
        {
            // 将C++ 传入的数据转化为C#的字节数据
            byte[] buffer = new byte[len];
            Marshal.Copy(data,buffer,0,len);
            obj.OnNetMsgBytes(buffer);
        }
    }

    [DllImport("EasyTcpClientPulgin")]
    private static extern IntPtr CellClient_Create(IntPtr csObj,OnNetMsgCallBack cbfun);
    [DllImport("EasyTcpClientPulgin")]
    private static extern bool CellClient_Connect(IntPtr cppClientObj, string ip, short port);
    [DllImport("EasyTcpClientPulgin")]
    private static extern bool CellClient_OnRun(IntPtr cppClientObj);
    [DllImport("EasyTcpClientPulgin")]
    private static extern void CellClient_Close(IntPtr cppClientObj);
    [DllImport("EasyTcpClientPulgin")]
    private static extern int CellClient_SendData(IntPtr cppClientObj, byte[] data, int len);

    private GCHandle _handleThis;
    // this对象指针 在c++消息回调中传回
    IntPtr _csThisObj = IntPtr.Zero;
    //C++ NativeTCPClient 对象指针
    IntPtr _cppClientObj = IntPtr.Zero;

    public void Create()
    {
        _handleThis = GCHandle.Alloc(this);
        _csThisObj = GCHandle.ToIntPtr(_handleThis);
        _cppClientObj = CellClient_Create(GCHandle.ToIntPtr(_handleThis), OnNetMsgCallBack1);
    }
    public bool Connect(string ip, short port)
    {
        if (_cppClientObj == IntPtr.Zero)
            return false;
        return CellClient_Connect(_cppClientObj, ip, port);
    }

    public bool OnRun()
    {
        if (_cppClientObj == IntPtr.Zero)
            return false;
        return CellClient_OnRun(_cppClientObj);
    }

    public void Close()
    {
        if (_cppClientObj == IntPtr.Zero)
            return;

        CellClient_Close(_cppClientObj);
        _cppClientObj = IntPtr.Zero;
        _handleThis.Free();
    }

    public int SendData(byte[] data)
    {
        return CellClient_SendData(_cppClientObj, data,data.Length);
    }

    // 由子类实现字节流解析
    public virtual void OnNetMsgBytes(byte[] data)
    {

    }

    // Start is called before the first frame update
    void Start()
    {
        //Debug.Log("1+2=" + Add(1,2));
        //Debug.Log("2-1=" + Sub(2, 1));
    }

    // Update is called once per frame
    void Update()
    {

    }
}
