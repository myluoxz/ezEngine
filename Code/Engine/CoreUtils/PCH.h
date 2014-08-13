#pragma once

// <StaticLinkUtil::StartHere>
// all include's before this will be left alone and not replaced by the StaticLinkUtil
// all include's AFTER this will be removed by the StaticLinkUtil and updated by what is actually used throughout the library



#include <Foundation/Basics.h>
#include <Foundation/Basics/Assert.h>
#include <Foundation/Basics/Types.h>
#include <Foundation/Basics/Types/Bitflags.h>
#include <Foundation/Communication/Telemetry.h>
#include <Foundation/Configuration/SubSystem.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Containers/Map.h>
#include <Foundation/Containers/Set.h>
#include <Foundation/Containers/StaticArray.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/IO/Stream.h>
#include <Foundation/Logging/Log.h>
#include <Foundation/Math/BoundingBox.h>
#include <Foundation/Math/Color8UNorm.h>
#include <Foundation/Math/Frustum.h>
#include <Foundation/Math/Mat4.h>
#include <Foundation/Math/Math.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/PCH.h>
#include <Foundation/Strings/PathUtils.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Strings/StringView.h>
#include <Foundation/Utilities/EnumerableClass.h>

