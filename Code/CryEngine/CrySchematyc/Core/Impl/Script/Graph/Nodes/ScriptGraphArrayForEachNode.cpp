// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "Script/Graph/Nodes/ScriptGraphArrayForEachNode.h"

#include <Schematyc/Compiler/IGraphNodeCompiler.h>
#include <Schematyc/Env/Elements/IEnvDataType.h>
#include <Schematyc/Utils/Any.h>
#include <Schematyc/Utils/AnyArray.h>
#include <Schematyc/Utils/StackString.h>

#include "Script/ScriptView.h"
#include "Script/Graph/ScriptGraphNode.h"
#include "Script/Graph/ScriptGraphNodeFactory.h"
#include "SerializationUtils/SerializationContext.h"

namespace Schematyc
{
CScriptGraphArrayForEachNode::SRuntimeData::SRuntimeData()
	: pos(InvalidIdx)
{}

CScriptGraphArrayForEachNode::SRuntimeData::SRuntimeData(const SRuntimeData& rhs)
	: pos(rhs.pos)
{}

SGUID CScriptGraphArrayForEachNode::SRuntimeData::ReflectSchematycType(CTypeInfo<SRuntimeData>& typeInfo)
{
	return "3270fe0e-fd07-46cb-9ac5-99ee7d03f55a"_schematyc_guid;
}

CScriptGraphArrayForEachNode::CScriptGraphArrayForEachNode(const SElementId& typeId)
	: m_defaultValue(typeId)
{}

SGUID CScriptGraphArrayForEachNode::GetTypeGUID() const
{
	return ms_typeGUID;
}

void CScriptGraphArrayForEachNode::CreateLayout(CScriptGraphNodeLayout& layout)
{
	layout.SetStyleId("Core::FlowControl");

	layout.AddInput("In", SGUID(), { EScriptGraphPortFlags::Flow, EScriptGraphPortFlags::MultiLink });
	layout.AddOutput("Out", SGUID(), EScriptGraphPortFlags::Flow);
	layout.AddOutput("Loop", SGUID(), EScriptGraphPortFlags::Flow);

	const char* szSubject = g_szNoType;
	if (!m_defaultValue.IsEmpty())
	{
		szSubject = m_defaultValue.GetTypeName();

		const SGUID typeGUID = m_defaultValue.GetTypeId().guid;
		layout.AddInput("Array", typeGUID, { EScriptGraphPortFlags::Data, EScriptGraphPortFlags::Array });
		layout.AddOutputWithData(m_defaultValue.GetTypeName(), typeGUID, { EScriptGraphPortFlags::Data, EScriptGraphPortFlags::MultiLink }, *m_defaultValue.GetValue());
	}
	layout.SetName("Array - For Each", szSubject);
}

void CScriptGraphArrayForEachNode::Compile(SCompilerContext& context, IGraphNodeCompiler& compiler) const
{
	compiler.BindCallback(&Execute);
	compiler.BindData(SRuntimeData());
}

void CScriptGraphArrayForEachNode::LoadDependencies(Serialization::IArchive& archive, const ISerializationContext& context)
{
	m_defaultValue.SerializeTypeId(archive);
}

void CScriptGraphArrayForEachNode::Save(Serialization::IArchive& archive, const ISerializationContext& context)
{
	m_defaultValue.SerializeTypeId(archive);
}

void CScriptGraphArrayForEachNode::Edit(Serialization::IArchive& archive, const ISerializationContext& context)
{
	{
		ScriptVariableData::CScopedSerializationConfig serializationConfig(archive);

		const SGUID guid = CScriptGraphNodeModel::GetNode().GetGraph().GetElement().GetGUID();
		serializationConfig.DeclareEnvDataTypes(guid);
		serializationConfig.DeclareScriptEnums(guid);
		serializationConfig.DeclareScriptStructs(guid);

		m_defaultValue.SerializeTypeId(archive);
	}
}

void CScriptGraphArrayForEachNode::RemapDependencies(IGUIDRemapper& guidRemapper)
{
	m_defaultValue.RemapDependencies(guidRemapper);
}

void CScriptGraphArrayForEachNode::Register(CScriptGraphNodeFactory& factory)
{
	class CCreator : public IScriptGraphNodeCreator
	{
	private:

		class CCreationCommand : public IScriptGraphNodeCreationCommand
		{
		public:

			inline CCreationCommand(const char* szSubject = g_szNoType, const SElementId& typeId = SElementId())
				: m_subject(szSubject)
				, m_typeId(typeId)
			{}

			// IScriptGraphNodeCreationCommand

			virtual const char* GetBehavior() const override
			{
				return "Array::For Each";
			}

			virtual const char* GetSubject() const override
			{
				return m_subject.c_str();
			}

			virtual const char* GetDescription() const override
			{
				return "Iterate through all elements in array";
			}

			virtual const char* GetStyleId() const override
			{
				return "Core::FlowControl";
			}

			virtual IScriptGraphNodePtr Execute(const Vec2& pos) override
			{
				return std::make_shared<CScriptGraphNode>(gEnv->pSchematyc->CreateGUID(), stl::make_unique<CScriptGraphArrayForEachNode>(m_typeId), pos);
			}

			// ~IScriptGraphNodeCreationCommand

		private:

			string     m_subject;
			SElementId m_typeId;
		};

	public:

		// IScriptGraphNodeCreator

		virtual SGUID GetTypeGUID() const override
		{
			return CScriptGraphArrayForEachNode::ms_typeGUID;
		}

		virtual IScriptGraphNodePtr CreateNode(const SGUID& guid) override
		{
			return std::make_shared<CScriptGraphNode>(guid, stl::make_unique<CScriptGraphArrayForEachNode>());
		}

		virtual void PopulateNodeCreationMenu(IScriptGraphNodeCreationMenu& nodeCreationMenu, const IScriptView& scriptView, const IScriptGraph& graph) override
		{
			nodeCreationMenu.AddCommand(std::make_shared<CCreationCommand>());

			// #SchematycTODO : This code is duplicated in all array nodes, find a way to reduce the duplication.

			auto visitEnvDataType = [&nodeCreationMenu, &scriptView](const IEnvDataType& envDataType) -> EVisitStatus
			{
				CStackString subject;
				scriptView.QualifyName(envDataType, subject);
				nodeCreationMenu.AddCommand(std::make_shared<CCreationCommand>(subject.c_str(), SElementId(EDomain::Env, envDataType.GetGUID())));
				return EVisitStatus::Continue;
			};
			scriptView.VisitEnvDataTypes(EnvDataTypeConstVisitor::FromLambda(visitEnvDataType));
		}

		// ~IScriptGraphNodeCreator
	};

	factory.RegisterCreator(std::make_shared<CCreator>());
}

SRuntimeResult CScriptGraphArrayForEachNode::Execute(SRuntimeContext& context, const SRuntimeActivationParams& activationParams)
{
	SRuntimeData& data = DynamicCast<SRuntimeData>(*context.node.GetData());
	if (activationParams.mode == EActivationMode::Input)
	{
		data.pos = 0;
	}

	CAnyArrayPtr pArray = DynamicCast<CAnyArrayPtr>(*context.node.GetInputData(EInputIdx::Array));
	if (data.pos < pArray->GetSize())
	{
		Any::CopyAssign(*context.node.GetOutputData(EOutputIdx::Value), (*pArray)[data.pos++]);
		return SRuntimeResult(ERuntimeStatus::ContinueAndRepeat, EOutputIdx::Loop);
	}
	else
	{
		return SRuntimeResult(ERuntimeStatus::Continue, EOutputIdx::Out);
	}
}

const SGUID CScriptGraphArrayForEachNode::ms_typeGUID = "67348889-afb6-4926-9275-9cb95e507787"_schematyc_guid;
} // Schematyc

SCHEMATYC_REGISTER_SCRIPT_GRAPH_NODE(Schematyc::CScriptGraphArrayForEachNode::Register)
