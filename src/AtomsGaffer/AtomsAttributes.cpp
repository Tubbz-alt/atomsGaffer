#include "AtomsGaffer/AtomsAttributes.h"
#include "AtomsGaffer/AtomsObject.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECore/TypeIds.h"

#include "AtomsCore/Metadata/MetadataTypeIds.h"
#include <AtomsCore/Metadata/Metadata.h>
#include <AtomsCore/Metadata/MetadataImpl.h>

#include "AtomsUtils/Utils.h"

#include <algorithm>
#include <AtomsUtils/Logger.h>


IE_CORE_DEFINERUNTIMETYPED( AtomsGaffer::AtomsAttributes );

using namespace IECore;
using namespace Gaffer;
using namespace GafferScene;
using namespace AtomsGaffer;

size_t AtomsAttributes::g_firstPlugIndex = 0;

AtomsAttributes::AtomsAttributes( const std::string &name ) : SceneProcessor( name )
{
    storeIndexOfNextChild( g_firstPlugIndex );

    addChild( new StringPlug( "agentIds", Plug::In ) );
    addChild( new BoolPlug( "invert", Plug::In, false ) );
    addChild( new CompoundDataPlug( "metadata" ) );

    // Fast pass-through for things we don't modify
    outPlug()->attributesPlug()->setInput( inPlug()->attributesPlug() );
    outPlug()->transformPlug()->setInput( inPlug()->transformPlug() );
    outPlug()->boundPlug()->setInput( inPlug()->boundPlug() );
    outPlug()->childNamesPlug()->setInput( inPlug()->childNamesPlug() );
    outPlug()->setNamesPlug()->setInput( inPlug()->setNamesPlug() );
    outPlug()->setPlug()->setInput( inPlug()->setPlug() );
    outPlug()->globalsPlug()->setInput( inPlug()->globalsPlug() );
}

Gaffer::StringPlug *AtomsAttributes::agentIdsPlug()
{
    return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *AtomsAttributes::agentIdsPlug() const
{
    return getChild<StringPlug>( g_firstPlugIndex );
}

Gaffer::BoolPlug *AtomsAttributes::invertPlug()
{
    return getChild<BoolPlug>( g_firstPlugIndex + 1);
}

const Gaffer::BoolPlug *AtomsAttributes::invertPlug() const
{
    return getChild<BoolPlug>( g_firstPlugIndex + 1);
}

Gaffer::CompoundDataPlug *AtomsAttributes::metadataPlug()
{
    return getChild<Gaffer::CompoundDataPlug>( g_firstPlugIndex + 2);
}

const Gaffer::CompoundDataPlug *AtomsAttributes::metadataPlug() const
{
    return getChild<Gaffer::CompoundDataPlug>( g_firstPlugIndex + 2 );
}

void AtomsAttributes::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
    if( input == inPlug()->objectPlug() ||
        input == inPlug()->attributesPlug() ||
        input == agentIdsPlug() ||
        input == invertPlug() ||
        metadataPlug()->isAncestorOf( input ))
    {
        outputs.push_back( outPlug()->objectPlug() );
    }
}

void AtomsAttributes::hashObject( const ScenePath &path, const Gaffer::Context *context, const GafferScene::ScenePlug *parent, IECore::MurmurHash &h ) const
{
    agentIdsPlug()->hash( h );
    invertPlug()->hash( h );
    metadataPlug()->hash( h );
    inPlug()->objectPlug()->hash( h );
    inPlug()->attributesPlug()->hash( h );
}

void AtomsAttributes::parseVisibleAgents( std::vector<int>& agentsFiltered,  std::vector<int>& agentIds, const std::string& filter, bool invert ) const
{
    std::vector<std::string> idsEntryStr;
    AtomsUtils::splitString(filter, ',', idsEntryStr);
    std::vector<int> idsToRemove;
    std::vector<int> idsToAdd;
    std::sort(agentIds.begin(), agentIds.end());
    for ( unsigned int eId = 0; eId < idsEntryStr.size(); eId++ )
    {
        std::string currentStr = idsEntryStr[eId];
        currentStr = AtomsUtils::eraseFromString(currentStr, ' ');
        if (currentStr.length() == 0)
        {
            continue;
        }

        std::vector<int>* currentIdArray = &idsToAdd;
        if (invert)
            currentIdArray = &idsToRemove;
        // is a remove id
        if ( currentStr[0] == '!' )
        {
            if ( invert )
            {
                currentIdArray = &idsToAdd;
            }
            else
            {
                currentIdArray = &idsToRemove;
            }
            currentStr = currentStr.substr( 1, currentStr.length() );
        }

        currentStr = AtomsUtils::eraseFromString( currentStr, '(' );
        currentStr = AtomsUtils::eraseFromString( currentStr, ')' );

        // it is a range
        if ( currentStr.find( '-' ) != std::string::npos )
        {
            std::vector<std::string> rangeEntryStr;
            AtomsUtils::splitString( currentStr, '-', rangeEntryStr );

            int startRange = 0;
            int endRange = 0;
            if ( !rangeEntryStr.empty() )
            {
                startRange = atoi( rangeEntryStr[0].c_str() );

                if (rangeEntryStr.size() > 1)
                {
                    endRange = atoi( rangeEntryStr[1].c_str() );
                    if ( startRange < endRange )
                    {
                        for ( int rId = startRange; rId <= endRange; rId++ )
                        {
                            currentIdArray->push_back( rId );
                        }
                    }
                }
            }
        }
        else
        {
            int tmpId = atoi( currentStr.c_str() );
            currentIdArray->push_back( tmpId );
        }
    }

    agentsFiltered.clear();
    std::sort( idsToAdd.begin(), idsToAdd.end() );
    std::sort( idsToRemove.begin(), idsToRemove.end() );
    if ( !idsToAdd.empty() )
    {
        std::vector<int> validIds;
        std::set_intersection(
                agentIds.begin(), agentIds.end(),
                idsToAdd.begin(), idsToAdd.end(),
                std::back_inserter( validIds )
        );


        std::set_difference(
                validIds.begin(), validIds.end(),
                idsToRemove.begin(), idsToRemove.end(),
                std::back_inserter( agentsFiltered )
        );
    }
    else
    {
        std::set_difference(
                agentIds.begin(), agentIds.end(),
                idsToRemove.begin(), idsToRemove.end(),
                std::back_inserter( agentsFiltered )
        );
    }
}

template <typename T, typename OUT, typename IN>
void AtomsAttributes::setMetadataOnPoints(
        IECoreScene::PointsPrimitivePtr& primitive,
        const std::string& metadataName,
        const std::vector<int>& agentIdVec,
        const std::vector<int>& agentsFiltered,
        std::map<int, int>& agentIdPointsMapper,
        const T& data,
        const T& defaultValue,
        ConstCompoundObjectPtr& attributes
        ) const
{
    typename OUT::Ptr  metadataVariable = nullptr;

    auto metadataVariableIt = primitive->variables.find( std::string( "atoms:" ) + metadataName );
    if( metadataVariableIt == primitive->variables.end() )
    {
        metadataVariable = new OUT();
        auto& defaultData = metadataVariable->writable();
        defaultData.resize( agentIdVec.size() );

        for ( size_t i = 0; i < defaultData.size(); ++i )
        {
            defaultData[i] = defaultValue;
        }

        //try to fill the data from the attributes
        if( attributes )
        {
            for ( size_t i = 0;  i < agentIdVec.size(); ++i )
            {
                int agentId = agentIdVec[i];


                auto atomsData = attributes->member<const AtomsObject>( "atoms:agents" );
                if( !atomsData )
                {
                    throw InvalidArgumentException( "AtomsAttributes : No agents data found." );
                }

                auto agentsData = atomsData->blindData();
                if( !agentsData )
                {
                    throw InvalidArgumentException( "AtomsAttributes : No agents data found." );
                }

                auto agentData = agentsData->member<const CompoundData>( std::to_string( agentId ) );
                if ( !agentData )
                {
                    continue;
                }

                auto metadataData = agentData->member<const CompoundData>( "metadata" );
                if ( !metadataData )
                {
                    continue;
                }

                auto& atomsMetaData = metadataData->readable();
                auto atomsMetaIt = atomsMetaData.find( metadataName );
                if ( atomsMetaIt == atomsMetaData.cend() )
                {
                    continue;
                }


                if ( atomsMetaIt->second->typeId() == IN::staticTypeId() )
                {
                    defaultData[i] = T(runTimeCast<const IN>( atomsMetaIt->second )->readable());
                }

            }
        }

        primitive->variables[std::string( "atoms:" ) + metadataName] = IECoreScene::PrimitiveVariable( IECoreScene::PrimitiveVariable::Vertex, metadataVariable );
    }
    else
    {
        if ( metadataVariableIt->second.interpolation != IECoreScene::PrimitiveVariable::Vertex )
        {
            return;
        }

        metadataVariable = runTimeCast<OUT>( metadataVariableIt->second.data );
    }

    if ( !metadataVariable )
    {
        return;
    }

    auto& metadataVec = metadataVariable->writable();

    for ( size_t i = 0; i < agentsFiltered.size(); ++i )
    {
        metadataVec[agentIdPointsMapper[agentsFiltered[i]]] = data;
    }
}

IECore::ConstObjectPtr AtomsAttributes::computeObject( const ScenePath &path, const Gaffer::Context *context, const GafferScene::ScenePlug *parent ) const
{
    auto inputObject = inPlug()->objectPlug()->getValue();
    auto attributes = inPlug()->attributesPlug()->getValue();

    auto crowd = runTimeCast<const IECoreScene::PointsPrimitive>( inputObject);
    if( !crowd )
    {
        return inputObject;
    }

    IECore::ObjectPtr result = inputObject->copy();
    auto outCrowd = runTimeCast<IECoreScene::PointsPrimitive>( result );

    IECore::CompoundDataMap compoundDataMap;
    metadataPlug()->fillCompoundData( compoundDataMap );

    const auto agentId = crowd->variables.find( "atoms:agentId" );
    if( agentId == crowd->variables.end() )
    {
        throw InvalidArgumentException( "AtomsAttributes : Input must be a PointsPrimitive containing an \"atoms:agentId\" vertex variable" );
    }

    auto agentIdData = runTimeCast<const IntVectorData>( agentId->second.data );
    if (!agentIdData)
    {
        throw InvalidArgumentException( "AtomsAttributes : Input must be a PointsPrimitive containing an \"atoms:agentId\" vertex variable" );
    }

    std::vector<int> agentIdVec = agentIdData->readable();
    std::map<int, int> agentIdPointsMapper;
    for( size_t i=0; i< agentIdVec.size(); ++i )
    {
        agentIdPointsMapper[agentIdVec[i]] = i;
    }

    // Filter the agents
    std::vector<int> agentsFiltered;
    parseVisibleAgents( agentsFiltered, agentIdVec, AtomsUtils::eraseFromString(agentIdsPlug()->getValue(), ' '), invertPlug()->getValue() );

    for( auto it = compoundDataMap.cbegin(); it != compoundDataMap.cend(); ++it )
    {
        const std::string& metadataName = it->first.string();

        ConstCompoundObjectPtr attributesData = runTimeCast<const CompoundObject>( attributes );

        switch( it->second->typeId() )
        {
            case IECore::TypeId::BoolDataTypeId:
            {
                auto data = runTimeCast<const BoolData>( it->second );
                setMetadataOnPoints<bool, BoolVectorData, BoolData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), false, attributesData
                        );
                break;
            }

            case IECore::TypeId::IntDataTypeId:
            {
                auto data = runTimeCast<const IntData>( it->second );
                setMetadataOnPoints<int, IntVectorData, IntData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), 0, attributesData
                        );
                break;
            }

            case IECore::TypeId::FloatDataTypeId:
            {
                auto data = runTimeCast<const FloatData>( it->second );
                setMetadataOnPoints<float, FloatVectorData, DoubleData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), 0.0, attributesData
                        );
                break;
            }

            case IECore::TypeId::StringDataTypeId:
            {
                auto data = runTimeCast<const StringData>( it->second );
                setMetadataOnPoints<std::string, StringVectorData, StringData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), "", attributesData
                        );
                break;
            }

            case IECore::TypeId::V2fDataTypeId:
            {
                auto data = runTimeCast<const V2fData>( it->second );
                setMetadataOnPoints<Imath::V2f, V2fVectorData, V2dData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), Imath::V2f( 0.0, 0.0 ), attributesData
                        );
                break;
            }

            case IECore::TypeId::V3fDataTypeId:
            {
                auto data = runTimeCast<const V3fData>( it->second );
                setMetadataOnPoints<Imath::V3f, V3fVectorData, V3dData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), Imath::V3f( 0.0, 0.0, 0.0 ), attributesData
                        );
                break;
            }

            case IECore::TypeId::M44fDataTypeId:
            {
                auto data = runTimeCast<const M44fData>( it->second );
                setMetadataOnPoints<Imath::M44f, M44fVectorData, M44dData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), Imath::M44f(), attributesData
                        );
                break;
            }

            case IECore::TypeId::QuatfDataTypeId:
            {
                auto data = runTimeCast<const QuatfData>( it->second );
                setMetadataOnPoints<Imath::Quatf, QuatfVectorData, QuatfData>(
                        outCrowd, metadataName, agentIdVec, agentsFiltered,
                        agentIdPointsMapper, data->readable(), Imath::Quatf(), attributesData
                        );
                break;
            }

            default:
            {
                break;
            }
        }
    }

    return result;
}
