﻿using UnityEngine;
using Ouroboros;
using System; 
using System.Collections;

namespace Ouroboros
{

/*
	Ouroboros's math related module
*/
public class OUROMath 
{
	public static float OURO_FLT_MAX = float.MaxValue;
	
	public static float int82angle(SByte angle, bool half)
	{
		float halfv = 128f;
		if(half == true)
			halfv = 254f;
		
		halfv = ((float)angle) * ((float)System.Math.PI / halfv);
		return halfv;
	}
	
	public static bool almostEqual(float f1, float f2, float epsilon)
	{
		return Math.Abs( f1 - f2 ) < epsilon;
	}

	public static bool isNumeric(object v)
	{
		return v is sbyte || v is byte ||
			v is short || v is ushort ||
			v is int || v is uint ||
			v is long || v is ulong ||
			v is char || v is decimal || v is float ||
			v is double || v is Int16 || v is Int64 ||
			v is UInt16 || v is UInt64 || 
			v is Boolean || v is bool;
	}
}


}
