/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libsolidity/analysis/ControlFlowAnalyzer.h>
#include <liblangutil/SourceLocation.h>

using namespace std;
using namespace langutil;
using namespace dev::solidity;

bool ControlFlowAnalyzer::analyze(ASTNode const& _astRoot)
{
	_astRoot.accept(*this);
	return Error::containsOnlyWarnings(m_errorReporter.errors());
}

bool ControlFlowAnalyzer::visit(FunctionDefinition const& _function)
{
	if (_function.isImplemented())
	{
		auto const& functionFlow = m_cfg.functionFlow(_function);
		checkUninitializedAccess(functionFlow.entry, functionFlow.exit);
	}
	return false;
}

void ControlFlowAnalyzer::checkUninitializedAccess(CFGNode const* _entry, CFGNode const* _exit) const
{
	struct NodeInfo
	{
		set<VariableDeclaration const*> unassignedVariables;
		set<VariableOccurrence const*> uninitializedVariableAccesses;
		/// Propagate the information from another node to this node.
		/// To be used to propagate information from a node to its exit nodes.
		/// Returns true, if new variables were added and the current node has to
		/// be traversed again.
		bool propagateFrom(NodeInfo const& _entryNode)
		{
			size_t previousUnassignedVariables = unassignedVariables.size();
			size_t previousUninitializedVariableAccessess = uninitializedVariableAccesses.size();
			unassignedVariables += _entryNode.unassignedVariables;
			uninitializedVariableAccesses += _entryNode.uninitializedVariableAccesses;
			return
				unassignedVariables.size() > previousUnassignedVariables ||
				uninitializedVariableAccesses.size() > previousUninitializedVariableAccessess
			;
		}
	};
	map<CFGNode const*, NodeInfo> nodeInfos;
	stack<CFGNode const*> nodesToTraverse;
	nodesToTraverse.push(_entry);

	// Walk all paths starting from the nodes in ``nodesToTraverse`` until ``NodeInfo::propagateFrom``
	// returns false for all exits, i.e. until all paths have been walked with maximal sets of unassigned
	// variables and accesses.
	while (!nodesToTraverse.empty())
	{
		auto currentNode = nodesToTraverse.top();
		nodesToTraverse.pop();

		auto& nodeInfo = nodeInfos[currentNode];
		for (auto const& variableOccurrence: currentNode->variableOccurrences)
		{
			switch (variableOccurrence.kind())
			{
				case VariableOccurrence::Kind::Assignment:
					nodeInfo.unassignedVariables.erase(&variableOccurrence.declaration());
					break;
				case VariableOccurrence::Kind::InlineAssembly:
					// We consider all variable referenced in inline assembly as assignments.
					// So far any reference is enough, but we might want to check whether
					// there actually was an assignment in the future.
					nodeInfo.unassignedVariables.erase(&variableOccurrence.declaration());
					break;
				case VariableOccurrence::Kind::Access:
					if (nodeInfo.unassignedVariables.count(&variableOccurrence.declaration()))
					{
						if (variableOccurrence.declaration().type()->dataStoredIn(DataLocation::Storage))
							// Merely store the unassigned access. We do not generate an error right away, since this
							// path might still always revert. It is only an error if this is propagated to the exit
							// node of the function (i.e. there is a path with an uninitialized access).
							nodeInfo.uninitializedVariableAccesses.insert(&variableOccurrence);
					}
					break;
				case VariableOccurrence::Kind::Declaration:
					nodeInfo.unassignedVariables.insert(&variableOccurrence.declaration());
					break;
			}
		}

		// Propagate changes to all exits and queue them for traversal, if needed.
		for (auto const& exit: currentNode->exits)
			if (nodeInfos[exit].propagateFrom(nodeInfo))
				nodesToTraverse.push(exit);
	}

	auto const& exitInfo = nodeInfos[_exit];
	if (!exitInfo.uninitializedVariableAccesses.empty())
	{
		vector<VariableOccurrence const*> uninitializedAccessesOrdered(
			exitInfo.uninitializedVariableAccesses.begin(),
			exitInfo.uninitializedVariableAccesses.end()
		 );
		sort(
			uninitializedAccessesOrdered.begin(),
			uninitializedAccessesOrdered.end(),
			[](VariableOccurrence const* lhs, VariableOccurrence const* rhs) -> bool
			{
				if (lhs->occurrence() && rhs->occurrence())
				{
					if (lhs->occurrence()->id() < rhs->occurrence()->id()) return true;
					if (rhs->occurrence()->id() < lhs->occurrence()->id()) return false;
				}
				else if (rhs->occurrence())
					return true;
				else if (lhs->occurrence())
					return false;

				if (lhs->declaration().id() < rhs->declaration().id()) return true;
				else if (rhs->declaration().id() < lhs->declaration().id()) return false;
				using KindCompareType = std::underlying_type<VariableOccurrence::Kind>::type;
				return static_cast<KindCompareType>(lhs->kind()) < static_cast<KindCompareType>(rhs->kind());
			}
		);

		for (auto const* variableOccurrence: uninitializedAccessesOrdered)
		{
			SecondarySourceLocation ssl;
			if (variableOccurrence->occurrence())
				ssl.append("The variable was declared here.", variableOccurrence->declaration().location());

			m_errorReporter.typeError(
				variableOccurrence->occurrence() ?
					variableOccurrence->occurrence()->location() :
					variableOccurrence->declaration().location(),
				ssl,
				"This variable is of storage pointer type and is accessed without prior assignment."
			);
		}
	}
}
