//===--- SemaCXXScopeSpec.cpp - Semantic Analysis for C++ scope specifiers-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements C++ semantic analysis for scope specifiers.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Template.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Sema/DeclSpec.h"
#include "TypeLocBuilder.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

/// \brief Find the current instantiation that associated with the given type.
static CXXRecordDecl *getCurrentInstantiationOf(QualType T, 
                                                DeclContext *CurContext) {
  if (T.isNull())
    return 0;

  const Type *Ty = T->getCanonicalTypeInternal().getTypePtr();
  if (const RecordType *RecordTy = dyn_cast<RecordType>(Ty)) {
    CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordTy->getDecl());
    if (!T->isDependentType())
      return Record;

    // This may be a member of a class template or class template partial
    // specialization. If it's part of the current semantic context, then it's
    // an injected-class-name;
    for (; !CurContext->isFileContext(); CurContext = CurContext->getParent())
      if (CurContext->Equals(Record))
        return Record;
    
    return 0;
  } else if (isa<InjectedClassNameType>(Ty))
    return cast<InjectedClassNameType>(Ty)->getDecl();
  else
    return 0;
}

/// \brief Compute the DeclContext that is associated with the given type.
///
/// \param T the type for which we are attempting to find a DeclContext.
///
/// \returns the declaration context represented by the type T,
/// or NULL if the declaration context cannot be computed (e.g., because it is
/// dependent and not the current instantiation).
DeclContext *Sema::computeDeclContext(QualType T) {
  if (!T->isDependentType())
    if (const TagType *Tag = T->getAs<TagType>())
      return Tag->getDecl();

  return ::getCurrentInstantiationOf(T, CurContext);
}

/// \brief Compute the DeclContext that is associated with the given
/// scope specifier.
///
/// \param SS the C++ scope specifier as it appears in the source
///
/// \param EnteringContext when true, we will be entering the context of
/// this scope specifier, so we can retrieve the declaration context of a
/// class template or class template partial specialization even if it is
/// not the current instantiation.
///
/// \returns the declaration context represented by the scope specifier @p SS,
/// or NULL if the declaration context cannot be computed (e.g., because it is
/// dependent and not the current instantiation).
DeclContext *Sema::computeDeclContext(const CXXScopeSpec &SS,
                                      bool EnteringContext) {
  if (!SS.isSet() || SS.isInvalid())
    return 0;

  NestedNameSpecifier *NNS
    = static_cast<NestedNameSpecifier *>(SS.getScopeRep());
  if (NNS->isDependent()) {
    // If this nested-name-specifier refers to the current
    // instantiation, return its DeclContext.
    if (CXXRecordDecl *Record = getCurrentInstantiationOf(NNS))
      return Record;

    if (EnteringContext) {
      const Type *NNSType = NNS->getAsType();
      if (!NNSType) {
        return 0;
      }

      // Look through type alias templates, per C++0x [temp.dep.type]p1.
      NNSType = Context.getCanonicalType(NNSType);
      if (const TemplateSpecializationType *SpecType
            = NNSType->getAs<TemplateSpecializationType>()) {
        // We are entering the context of the nested name specifier, so try to
        // match the nested name specifier to either a primary class template
        // or a class template partial specialization.
        if (ClassTemplateDecl *ClassTemplate
              = dyn_cast_or_null<ClassTemplateDecl>(
                            SpecType->getTemplateName().getAsTemplateDecl())) {
          QualType ContextType
            = Context.getCanonicalType(QualType(SpecType, 0));

          // If the type of the nested name specifier is the same as the
          // injected class name of the named class template, we're entering
          // into that class template definition.
          QualType Injected
            = ClassTemplate->getInjectedClassNameSpecialization();
          if (Context.hasSameType(Injected, ContextType))
            return ClassTemplate->getTemplatedDecl();

          // If the type of the nested name specifier is the same as the
          // type of one of the class template's class template partial
          // specializations, we're entering into the definition of that
          // class template partial specialization.
          if (ClassTemplatePartialSpecializationDecl *PartialSpec
                = ClassTemplate->findPartialSpecialization(ContextType))
            return PartialSpec;
        }
      } else if (const RecordType *RecordT = NNSType->getAs<RecordType>()) {
        // The nested name specifier refers to a member of a class template.
        return RecordT->getDecl();
      }
    }

    return 0;
  }

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    llvm_unreachable("Dependent nested-name-specifier has no DeclContext");

  case NestedNameSpecifier::Namespace:
    return NNS->getAsNamespace();

  case NestedNameSpecifier::NamespaceAlias:
    return NNS->getAsNamespaceAlias()->getNamespace();

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate: {
    const TagType *Tag = NNS->getAsType()->getAs<TagType>();
    assert(Tag && "Non-tag type in nested-name-specifier");
    return Tag->getDecl();
  }

  case NestedNameSpecifier::Global:
    return Context.getTranslationUnitDecl();
  }

  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

bool Sema::isDependentScopeSpecifier(const CXXScopeSpec &SS) {
  if (!SS.isSet() || SS.isInvalid())
    return false;

  NestedNameSpecifier *NNS
    = static_cast<NestedNameSpecifier *>(SS.getScopeRep());
  return NNS->isDependent();
}

// \brief Determine whether this C++ scope specifier refers to an
// unknown specialization, i.e., a dependent type that is not the
// current instantiation.
bool Sema::isUnknownSpecialization(const CXXScopeSpec &SS) {
  if (!isDependentScopeSpecifier(SS))
    return false;

  NestedNameSpecifier *NNS
    = static_cast<NestedNameSpecifier *>(SS.getScopeRep());
  return getCurrentInstantiationOf(NNS) == 0;
}

/// \brief If the given nested name specifier refers to the current
/// instantiation, return the declaration that corresponds to that
/// current instantiation (C++0x [temp.dep.type]p1).
///
/// \param NNS a dependent nested name specifier.
CXXRecordDecl *Sema::getCurrentInstantiationOf(NestedNameSpecifier *NNS) {
  assert(getLangOpts().CPlusPlus && "Only callable in C++");
  assert(NNS->isDependent() && "Only dependent nested-name-specifier allowed");

  if (!NNS->getAsType())
    return 0;

  QualType T = QualType(NNS->getAsType(), 0);
  return ::getCurrentInstantiationOf(T, CurContext);
}

/// \brief Require that the context specified by SS be complete.
///
/// If SS refers to a type, this routine checks whether the type is
/// complete enough (or can be made complete enough) for name lookup
/// into the DeclContext. A type that is not yet completed can be
/// considered "complete enough" if it is a class/struct/union/enum
/// that is currently being defined. Or, if we have a type that names
/// a class template specialization that is not a complete type, we
/// will attempt to instantiate that class template.
bool Sema::RequireCompleteDeclContext(CXXScopeSpec &SS,
                                      DeclContext *DC) {
  assert(DC != 0 && "given null context");

  TagDecl *tag = dyn_cast<TagDecl>(DC);

  // If this is a dependent type, then we consider it complete.
  if (!tag || tag->isDependentContext())
    return false;

  // If we're currently defining this type, then lookup into the
  // type is okay: don't complain that it isn't complete yet.
  QualType type = Context.getTypeDeclType(tag);
  const TagType *tagType = type->getAs<TagType>();
  if (tagType && tagType->isBeingDefined())
    return false;

  SourceLocation loc = SS.getLastQualifierNameLoc();
  if (loc.isInvalid()) loc = SS.getRange().getBegin();

  // The type must be complete.
  if (RequireCompleteType(loc, type,
                          PDiag(diag::err_incomplete_nested_name_spec)
                            << SS.getRange())) {
    SS.SetInvalid(SS.getRange());
    return true;
  }

  // Fixed enum types are complete, but they aren't valid as scopes
  // until we see a definition, so awkwardly pull out this special
  // case.
  const EnumType *enumType = dyn_cast_or_null<EnumType>(tagType);
  if (!enumType || enumType->getDecl()->isCompleteDefinition())
    return false;

  // Try to instantiate the definition, if this is a specialization of an
  // enumeration temploid.
  EnumDecl *ED = enumType->getDecl();
  if (EnumDecl *Pattern = ED->getInstantiatedFromMemberEnum()) {
    MemberSpecializationInfo *MSI = ED->getMemberSpecializationInfo();
    if (MSI->getTemplateSpecializationKind() != TSK_ExplicitSpecialization) {
      if (InstantiateEnum(loc, ED, Pattern, getTemplateInstantiationArgs(ED),
                          TSK_ImplicitInstantiation)) {
        SS.SetInvalid(SS.getRange());
        return true;
      }
      return false;
    }
  }

  Diag(loc, diag::err_incomplete_nested_name_spec)
    << type << SS.getRange();
  SS.SetInvalid(SS.getRange());
  return true;
}

bool Sema::ActOnCXXGlobalScopeSpecifier(Scope *S, SourceLocation CCLoc,
                                        CXXScopeSpec &SS) {
  SS.MakeGlobal(Context, CCLoc);
  return false;
}

/// \brief Determines whether the given declaration is an valid acceptable
/// result for name lookup of a nested-name-specifier.
bool Sema::isAcceptableNestedNameSpecifier(NamedDecl *SD) {
  if (!SD)
    return false;

  // Namespace and namespace aliases are fine.
  if (isa<NamespaceDecl>(SD) || isa<NamespaceAliasDecl>(SD))
    return true;

  if (!isa<TypeDecl>(SD))
    return false;

  // Determine whether we have a class (or, in C++11, an enum) or
  // a typedef thereof. If so, build the nested-name-specifier.
  QualType T = Context.getTypeDeclType(cast<TypeDecl>(SD));
  if (T->isDependentType())
    return true;
  else if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(SD)) {
    if (TD->getUnderlyingType()->isRecordType() ||
        (Context.getLangOpts().CPlusPlus0x &&
         TD->getUnderlyingType()->isEnumeralType()))
      return true;
  } else if (isa<RecordDecl>(SD) ||
             (Context.getLangOpts().CPlusPlus0x && isa<EnumDecl>(SD)))
    return true;

  return false;
}

/// \brief If the given nested-name-specifier begins with a bare identifier
/// (e.g., Base::), perform name lookup for that identifier as a
/// nested-name-specifier within the given scope, and return the result of that
/// name lookup.
NamedDecl *Sema::FindFirstQualifierInScope(Scope *S, NestedNameSpecifier *NNS) {
  if (!S || !NNS)
    return 0;

  while (NNS->getPrefix())
    NNS = NNS->getPrefix();

  if (NNS->getKind() != NestedNameSpecifier::Identifier)
    return 0;

  LookupResult Found(*this, NNS->getAsIdentifier(), SourceLocation(),
                     LookupNestedNameSpecifierName);
  LookupName(Found, S);
  assert(!Found.isAmbiguous() && "Cannot handle ambiguities here yet");

  if (!Found.isSingleResult())
    return 0;

  NamedDecl *Result = Found.getFoundDecl();
  if (isAcceptableNestedNameSpecifier(Result))
    return Result;

  return 0;
}

bool Sema::isNonTypeNestedNameSpecifier(Scope *S, CXXScopeSpec &SS,
                                        SourceLocation IdLoc,
                                        IdentifierInfo &II,
                                        ParsedType ObjectTypePtr) {
  QualType ObjectType = GetTypeFromParser(ObjectTypePtr);
  LookupResult Found(*this, &II, IdLoc, LookupNestedNameSpecifierName);
  
  // Determine where to perform name lookup
  DeclContext *LookupCtx = 0;
  bool isDependent = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert(!SS.isSet() && "ObjectType and scope specifier cannot coexist");
    LookupCtx = computeDeclContext(ObjectType);
    isDependent = ObjectType->isDependentType();
  } else if (SS.isSet()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so long into the context associated with the prior nested-name-specifier.
    LookupCtx = computeDeclContext(SS, false);
    isDependent = isDependentScopeSpecifier(SS);
    Found.setContextRange(SS.getRange());
  }
  
  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.
    
    // The declaration context must be complete.
    if (!LookupCtx->isDependentContext() &&
        RequireCompleteDeclContext(SS, LookupCtx))
      return false;
    
    LookupQualifiedName(Found, LookupCtx);
  } else if (isDependent) {
    return false;
  } else {
    LookupName(Found, S);
  }
  Found.suppressDiagnostics();
  
  if (NamedDecl *ND = Found.getAsSingle<NamedDecl>())
    return isa<NamespaceDecl>(ND) || isa<NamespaceAliasDecl>(ND);
  
  return false;
}

namespace {

// Callback to only accept typo corrections that can be a valid C++ member
// intializer: either a non-static field member or a base class.
class NestedNameSpecifierValidatorCCC : public CorrectionCandidateCallback {
 public:
  explicit NestedNameSpecifierValidatorCCC(Sema &SRef)
      : SRef(SRef) {}

  virtual bool ValidateCandidate(const TypoCorrection &candidate) {
    return SRef.isAcceptableNestedNameSpecifier(candidate.getCorrectionDecl());
  }

 private:
  Sema &SRef;
};

}

/// \brief Build a new nested-name-specifier for "identifier::", as described
/// by ActOnCXXNestedNameSpecifier.
///
/// This routine differs only slightly from ActOnCXXNestedNameSpecifier, in
/// that it contains an extra parameter \p ScopeLookupResult, which provides
/// the result of name lookup within the scope of the nested-name-specifier
/// that was computed at template definition time.
///
/// If ErrorRecoveryLookup is true, then this call is used to improve error
/// recovery.  This means that it should not emit diagnostics, it should
/// just return true on failure.  It also means it should only return a valid
/// scope if it *knows* that the result is correct.  It should not return in a
/// dependent context, for example. Nor will it extend \p SS with the scope
/// specifier.
bool Sema::BuildCXXNestedNameSpecifier(Scope *S,
                                       IdentifierInfo &Identifier,
                                       SourceLocation IdentifierLoc,
                                       SourceLocation CCLoc,
                                       QualType ObjectType,
                                       bool EnteringContext,
                                       CXXScopeSpec &SS,
                                       NamedDecl *ScopeLookupResult,
                                       bool ErrorRecoveryLookup) {
  LookupResult Found(*this, &Identifier, IdentifierLoc, 
                     LookupNestedNameSpecifierName);

  // Determine where to perform name lookup
  DeclContext *LookupCtx = 0;
  bool isDependent = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert(!SS.isSet() && "ObjectType and scope specifier cannot coexist");
    LookupCtx = computeDeclContext(ObjectType);
    isDependent = ObjectType->isDependentType();
  } else if (SS.isSet()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so look into the context associated with the prior nested-name-specifier.
    LookupCtx = computeDeclContext(SS, EnteringContext);
    isDependent = isDependentScopeSpecifier(SS);
    Found.setContextRange(SS.getRange());
  }


  bool ObjectTypeSearchedInScope = false;
  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.

    // The declaration context must be complete.
    if (!LookupCtx->isDependentContext() &&
        RequireCompleteDeclContext(SS, LookupCtx))
      return true;

    LookupQualifiedName(Found, LookupCtx);

    if (!ObjectType.isNull() && Found.empty()) {
      // C++ [basic.lookup.classref]p4:
      //   If the id-expression in a class member access is a qualified-id of
      //   the form
      //
      //        class-name-or-namespace-name::...
      //
      //   the class-name-or-namespace-name following the . or -> operator is
      //   looked up both in the context of the entire postfix-expression and in
      //   the scope of the class of the object expression. If the name is found
      //   only in the scope of the class of the object expression, the name
      //   shall refer to a class-name. If the name is found only in the
      //   context of the entire postfix-expression, the name shall refer to a
      //   class-name or namespace-name. [...]
      //
      // Qualified name lookup into a class will not find a namespace-name,
      // so we do not need to diagnose that case specifically. However,
      // this qualified name lookup may find nothing. In that case, perform
      // unqualified name lookup in the given scope (if available) or
      // reconstruct the result from when name lookup was performed at template
      // definition time.
      if (S)
        LookupName(Found, S);
      else if (ScopeLookupResult)
        Found.addDecl(ScopeLookupResult);

      ObjectTypeSearchedInScope = true;
    }
  } else if (!isDependent) {
    // Perform unqualified name lookup in the current scope.
    LookupName(Found, S);
  }

  // If we performed lookup into a dependent context and did not find anything,
  // that's fine: just build a dependent nested-name-specifier.
  if (Found.empty() && isDependent &&
      !(LookupCtx && LookupCtx->isRecord() &&
        (!cast<CXXRecordDecl>(LookupCtx)->hasDefinition() ||
         !cast<CXXRecordDecl>(LookupCtx)->hasAnyDependentBases()))) {
    // Don't speculate if we're just trying to improve error recovery.
    if (ErrorRecoveryLookup)
      return true;
    
    // We were not able to compute the declaration context for a dependent
    // base object type or prior nested-name-specifier, so this
    // nested-name-specifier refers to an unknown specialization. Just build
    // a dependent nested-name-specifier.
    SS.Extend(Context, &Identifier, IdentifierLoc, CCLoc);
    return false;
  } 
  
  // FIXME: Deal with ambiguities cleanly.

  if (Found.empty() && !ErrorRecoveryLookup) {
    // We haven't found anything, and we're not recovering from a
    // different kind of error, so look for typos.
    DeclarationName Name = Found.getLookupName();
    NestedNameSpecifierValidatorCCC Validator(*this);
    TypoCorrection Corrected;
    Found.clear();
    if ((Corrected = CorrectTypo(Found.getLookupNameInfo(),
                                 Found.getLookupKind(), S, &SS, Validator,
                                 LookupCtx, EnteringContext))) {
      std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
      std::string CorrectedQuotedStr(Corrected.getQuoted(getLangOpts()));
      if (LookupCtx)
        Diag(Found.getNameLoc(), diag::err_no_member_suggest)
          << Name << LookupCtx << CorrectedQuotedStr << SS.getRange()
          << FixItHint::CreateReplacement(Found.getNameLoc(), CorrectedStr);
      else
        Diag(Found.getNameLoc(), diag::err_undeclared_var_use_suggest)
          << Name << CorrectedQuotedStr
          << FixItHint::CreateReplacement(Found.getNameLoc(), CorrectedStr);
      
      if (NamedDecl *ND = Corrected.getCorrectionDecl()) {
        Diag(ND->getLocation(), diag::note_previous_decl) << CorrectedQuotedStr;
        Found.addDecl(ND);
      }
      Found.setLookupName(Corrected.getCorrection());
    } else {
      Found.setLookupName(&Identifier);
    }
  }

  NamedDecl *SD = Found.getAsSingle<NamedDecl>();
  if (isAcceptableNestedNameSpecifier(SD)) {
    if (!ObjectType.isNull() && !ObjectTypeSearchedInScope) {
      // C++ [basic.lookup.classref]p4:
      //   [...] If the name is found in both contexts, the
      //   class-name-or-namespace-name shall refer to the same entity.
      //
      // We already found the name in the scope of the object. Now, look
      // into the current scope (the scope of the postfix-expression) to
      // see if we can find the same name there. As above, if there is no
      // scope, reconstruct the result from the template instantiation itself.
      NamedDecl *OuterDecl;
      if (S) {
        LookupResult FoundOuter(*this, &Identifier, IdentifierLoc, 
                                LookupNestedNameSpecifierName);
        LookupName(FoundOuter, S);
        OuterDecl = FoundOuter.getAsSingle<NamedDecl>();
      } else
        OuterDecl = ScopeLookupResult;

      if (isAcceptableNestedNameSpecifier(OuterDecl) &&
          OuterDecl->getCanonicalDecl() != SD->getCanonicalDecl() &&
          (!isa<TypeDecl>(OuterDecl) || !isa<TypeDecl>(SD) ||
           !Context.hasSameType(
                            Context.getTypeDeclType(cast<TypeDecl>(OuterDecl)),
                               Context.getTypeDeclType(cast<TypeDecl>(SD))))) {
         if (ErrorRecoveryLookup)
           return true;

         Diag(IdentifierLoc, 
              diag::err_nested_name_member_ref_lookup_ambiguous)
           << &Identifier;
         Diag(SD->getLocation(), diag::note_ambig_member_ref_object_type)
           << ObjectType;
         Diag(OuterDecl->getLocation(), diag::note_ambig_member_ref_scope);

         // Fall through so that we'll pick the name we found in the object
         // type, since that's probably what the user wanted anyway.
       }
    }

    // If we're just performing this lookup for error-recovery purposes, 
    // don't extend the nested-name-specifier. Just return now.
    if (ErrorRecoveryLookup)
      return false;
    
    if (NamespaceDecl *Namespace = dyn_cast<NamespaceDecl>(SD)) {
      SS.Extend(Context, Namespace, IdentifierLoc, CCLoc);
      return false;
    }

    if (NamespaceAliasDecl *Alias = dyn_cast<NamespaceAliasDecl>(SD)) {
      SS.Extend(Context, Alias, IdentifierLoc, CCLoc);
      return false;
    }

    QualType T = Context.getTypeDeclType(cast<TypeDecl>(SD));
    TypeLocBuilder TLB;
    if (isa<InjectedClassNameType>(T)) {
      InjectedClassNameTypeLoc InjectedTL
        = TLB.push<InjectedClassNameTypeLoc>(T);
      InjectedTL.setNameLoc(IdentifierLoc);
    } else if (isa<RecordType>(T)) {
      RecordTypeLoc RecordTL = TLB.push<RecordTypeLoc>(T);
      RecordTL.setNameLoc(IdentifierLoc);
    } else if (isa<TypedefType>(T)) {
      TypedefTypeLoc TypedefTL = TLB.push<TypedefTypeLoc>(T);
      TypedefTL.setNameLoc(IdentifierLoc);
    } else if (isa<EnumType>(T)) {
      EnumTypeLoc EnumTL = TLB.push<EnumTypeLoc>(T);
      EnumTL.setNameLoc(IdentifierLoc);
    } else if (isa<TemplateTypeParmType>(T)) {
      TemplateTypeParmTypeLoc TemplateTypeTL
        = TLB.push<TemplateTypeParmTypeLoc>(T);
      TemplateTypeTL.setNameLoc(IdentifierLoc);
    } else if (isa<UnresolvedUsingType>(T)) {
      UnresolvedUsingTypeLoc UnresolvedTL
        = TLB.push<UnresolvedUsingTypeLoc>(T);
      UnresolvedTL.setNameLoc(IdentifierLoc);
    } else if (isa<SubstTemplateTypeParmType>(T)) {
      SubstTemplateTypeParmTypeLoc TL 
        = TLB.push<SubstTemplateTypeParmTypeLoc>(T);
      TL.setNameLoc(IdentifierLoc);
    } else if (isa<SubstTemplateTypeParmPackType>(T)) {
      SubstTemplateTypeParmPackTypeLoc TL
        = TLB.push<SubstTemplateTypeParmPackTypeLoc>(T);
      TL.setNameLoc(IdentifierLoc);
    } else {
      llvm_unreachable("Unhandled TypeDecl node in nested-name-specifier");
    }

    if (T->isEnumeralType())
      Diag(IdentifierLoc, diag::warn_cxx98_compat_enum_nested_name_spec);

    SS.Extend(Context, SourceLocation(), TLB.getTypeLocInContext(Context, T),
              CCLoc);
    return false;
  }

  // Otherwise, we have an error case.  If we don't want diagnostics, just
  // return an error now.
  if (ErrorRecoveryLookup)
    return true;

  // If we didn't find anything during our lookup, try again with
  // ordinary name lookup, which can help us produce better error
  // messages.
  if (Found.empty()) {
    Found.clear(LookupOrdinaryName);
    LookupName(Found, S);
  }

  // In Microsoft mode, if we are within a templated function and we can't
  // resolve Identifier, then extend the SS with Identifier. This will have 
  // the effect of resolving Identifier during template instantiation. 
  // The goal is to be able to resolve a function call whose
  // nested-name-specifier is located inside a dependent base class.
  // Example: 
  //
  // class C {
  // public:
  //    static void foo2() {  }
  // };
  // template <class T> class A { public: typedef C D; };
  //
  // template <class T> class B : public A<T> {
  // public:
  //   void foo() { D::foo2(); }
  // };
  if (getLangOpts().MicrosoftExt) {
    DeclContext *DC = LookupCtx ? LookupCtx : CurContext;
    if (DC->isDependentContext() && DC->isFunctionOrMethod()) {
      SS.Extend(Context, &Identifier, IdentifierLoc, CCLoc);
      return false;
    }
  }

  unsigned DiagID;
  if (!Found.empty())
    DiagID = diag::err_expected_class_or_namespace;
  else if (SS.isSet()) {
    Diag(IdentifierLoc, diag::err_no_member) 
      << &Identifier << LookupCtx << SS.getRange();
    return true;
  } else
    DiagID = diag::err_undeclared_var_use;

  if (SS.isSet())
    Diag(IdentifierLoc, DiagID) << &Identifier << SS.getRange();
  else
    Diag(IdentifierLoc, DiagID) << &Identifier;

  return true;
}

bool Sema::ActOnCXXNestedNameSpecifier(Scope *S,
                                       IdentifierInfo &Identifier,
                                       SourceLocation IdentifierLoc,
                                       SourceLocation CCLoc,
                                       ParsedType ObjectType,
                                       bool EnteringContext,
                                       CXXScopeSpec &SS) {
  if (SS.isInvalid())
    return true;
  
  return BuildCXXNestedNameSpecifier(S, Identifier, IdentifierLoc, CCLoc,
                                     GetTypeFromParser(ObjectType),
                                     EnteringContext, SS, 
                                     /*ScopeLookupResult=*/0, false);
}

bool Sema::ActOnCXXNestedNameSpecifierDecltype(CXXScopeSpec &SS,
                                               const DeclSpec &DS,
                                               SourceLocation ColonColonLoc) {
  if (SS.isInvalid() || DS.getTypeSpecType() == DeclSpec::TST_error)
    return true;

  assert(DS.getTypeSpecType() == DeclSpec::TST_decltype);

  QualType T = BuildDecltypeType(DS.getRepAsExpr(), DS.getTypeSpecTypeLoc());
  if (!T->isDependentType() && !T->getAs<TagType>()) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_expected_class) 
      << T << getLangOpts().CPlusPlus;
    return true;
  }

  TypeLocBuilder TLB;
  DecltypeTypeLoc DecltypeTL = TLB.push<DecltypeTypeLoc>(T);
  DecltypeTL.setNameLoc(DS.getTypeSpecTypeLoc());
  SS.Extend(Context, SourceLocation(), TLB.getTypeLocInContext(Context, T),
            ColonColonLoc);
  return false;
}

/// IsInvalidUnlessNestedName - This method is used for error recovery
/// purposes to determine whether the specified identifier is only valid as
/// a nested name specifier, for example a namespace name.  It is
/// conservatively correct to always return false from this method.
///
/// The arguments are the same as those passed to ActOnCXXNestedNameSpecifier.
bool Sema::IsInvalidUnlessNestedName(Scope *S, CXXScopeSpec &SS,
                                     IdentifierInfo &Identifier, 
                                     SourceLocation IdentifierLoc,
                                     SourceLocation ColonLoc,
                                     ParsedType ObjectType,
                                     bool EnteringContext) {
  if (SS.isInvalid())
    return false;
  
  return !BuildCXXNestedNameSpecifier(S, Identifier, IdentifierLoc, ColonLoc,
                                      GetTypeFromParser(ObjectType),
                                      EnteringContext, SS, 
                                      /*ScopeLookupResult=*/0, true);
}

bool Sema::ActOnCXXNestedNameSpecifier(Scope *S,
                                       CXXScopeSpec &SS,
                                       SourceLocation TemplateKWLoc,
                                       TemplateTy Template,
                                       SourceLocation TemplateNameLoc,
                                       SourceLocation LAngleLoc,
                                       ASTTemplateArgsPtr TemplateArgsIn,
                                       SourceLocation RAngleLoc,
                                       SourceLocation CCLoc,
                                       bool EnteringContext) {
  if (SS.isInvalid())
    return true;
  
  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  if (DependentTemplateName *DTN = Template.get().getAsDependentTemplateName()){
    // Handle a dependent template specialization for which we cannot resolve
    // the template name.
    assert(DTN->getQualifier()
             == static_cast<NestedNameSpecifier*>(SS.getScopeRep()));
    QualType T = Context.getDependentTemplateSpecializationType(ETK_None,
                                                          DTN->getQualifier(),
                                                          DTN->getIdentifier(),
                                                                TemplateArgs);
    
    // Create source-location information for this type.
    TypeLocBuilder Builder;
    DependentTemplateSpecializationTypeLoc SpecTL
      = Builder.push<DependentTemplateSpecializationTypeLoc>(T);
    SpecTL.setElaboratedKeywordLoc(SourceLocation());
    SpecTL.setQualifierLoc(SS.getWithLocInContext(Context));
    SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
    SpecTL.setTemplateNameLoc(TemplateNameLoc);
    SpecTL.setLAngleLoc(LAngleLoc);
    SpecTL.setRAngleLoc(RAngleLoc);
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());
    
    SS.Extend(Context, TemplateKWLoc, Builder.getTypeLocInContext(Context, T),
              CCLoc);
    return false;
  }
  
  
  if (Template.get().getAsOverloadedTemplate() ||
      isa<FunctionTemplateDecl>(Template.get().getAsTemplateDecl())) {
    SourceRange R(TemplateNameLoc, RAngleLoc);
    if (SS.getRange().isValid())
      R.setBegin(SS.getRange().getBegin());
      
    Diag(CCLoc, diag::err_non_type_template_in_nested_name_specifier)
      << Template.get() << R;
    NoteAllFoundTemplates(Template.get());
    return true;
  }
                                
  // We were able to resolve the template name to an actual template. 
  // Build an appropriate nested-name-specifier.
  QualType T = CheckTemplateIdType(Template.get(), TemplateNameLoc, 
                                   TemplateArgs);
  if (T.isNull())
    return true;

  // Alias template specializations can produce types which are not valid
  // nested name specifiers.
  if (!T->isDependentType() && !T->getAs<TagType>()) {
    Diag(TemplateNameLoc, diag::err_nested_name_spec_non_tag) << T;
    NoteAllFoundTemplates(Template.get());
    return true;
  }

  // Provide source-location information for the template specialization type.
  TypeLocBuilder Builder;
  TemplateSpecializationTypeLoc SpecTL
    = Builder.push<TemplateSpecializationTypeLoc>(T);
  SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
  SpecTL.setTemplateNameLoc(TemplateNameLoc);
  SpecTL.setLAngleLoc(LAngleLoc);
  SpecTL.setRAngleLoc(RAngleLoc);
  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());


  SS.Extend(Context, TemplateKWLoc, Builder.getTypeLocInContext(Context, T),
            CCLoc);
  return false;
}

namespace {
  /// \brief A structure that stores a nested-name-specifier annotation,
  /// including both the nested-name-specifier 
  struct NestedNameSpecifierAnnotation {
    NestedNameSpecifier *NNS;
  };
}

void *Sema::SaveNestedNameSpecifierAnnotation(CXXScopeSpec &SS) {
  if (SS.isEmpty() || SS.isInvalid())
    return 0;
  
  void *Mem = Context.Allocate((sizeof(NestedNameSpecifierAnnotation) +
                                                        SS.location_size()),
                               llvm::alignOf<NestedNameSpecifierAnnotation>());
  NestedNameSpecifierAnnotation *Annotation
    = new (Mem) NestedNameSpecifierAnnotation;
  Annotation->NNS = SS.getScopeRep();
  memcpy(Annotation + 1, SS.location_data(), SS.location_size());
  return Annotation;
}

void Sema::RestoreNestedNameSpecifierAnnotation(void *AnnotationPtr, 
                                                SourceRange AnnotationRange,
                                                CXXScopeSpec &SS) {
  if (!AnnotationPtr) {
    SS.SetInvalid(AnnotationRange);
    return;
  }
  
  NestedNameSpecifierAnnotation *Annotation
    = static_cast<NestedNameSpecifierAnnotation *>(AnnotationPtr);
  SS.Adopt(NestedNameSpecifierLoc(Annotation->NNS, Annotation + 1));
}

bool Sema::ShouldEnterDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");

  NestedNameSpecifier *Qualifier =
    static_cast<NestedNameSpecifier*>(SS.getScopeRep());

  // There are only two places a well-formed program may qualify a
  // declarator: first, when defining a namespace or class member
  // out-of-line, and second, when naming an explicitly-qualified
  // friend function.  The latter case is governed by
  // C++03 [basic.lookup.unqual]p10:
  //   In a friend declaration naming a member function, a name used
  //   in the function declarator and not part of a template-argument
  //   in a template-id is first looked up in the scope of the member
  //   function's class. If it is not found, or if the name is part of
  //   a template-argument in a template-id, the look up is as
  //   described for unqualified names in the definition of the class
  //   granting friendship.
  // i.e. we don't push a scope unless it's a class member.

  switch (Qualifier->getKind()) {
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    // These are always namespace scopes.  We never want to enter a
    // namespace scope from anything but a file context.
    return CurContext->getRedeclContext()->isFileContext();

  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    // These are never namespace scopes.
    return true;
  }

  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

/// ActOnCXXEnterDeclaratorScope - Called when a C++ scope specifier (global
/// scope or nested-name-specifier) is parsed, part of a declarator-id.
/// After this method is called, according to [C++ 3.4.3p3], names should be
/// looked up in the declarator-id's scope, until the declarator is parsed and
/// ActOnCXXExitDeclaratorScope is called.
/// The 'SS' should be a non-empty valid CXXScopeSpec.
bool Sema::ActOnCXXEnterDeclaratorScope(Scope *S, CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");

  if (SS.isInvalid()) return true;

  DeclContext *DC = computeDeclContext(SS, true);
  if (!DC) return true;

  // Before we enter a declarator's context, we need to make sure that
  // it is a complete declaration context.
  if (!DC->isDependentContext() && RequireCompleteDeclContext(SS, DC))
    return true;
    
  EnterDeclaratorContext(S, DC);

  // Rebuild the nested name specifier for the new scope.
  if (DC->isDependentContext())
    RebuildNestedNameSpecifierInCurrentInstantiation(SS);

  return false;
}

/// ActOnCXXExitDeclaratorScope - Called when a declarator that previously
/// invoked ActOnCXXEnterDeclaratorScope(), is finished. 'SS' is the same
/// CXXScopeSpec that was passed to ActOnCXXEnterDeclaratorScope as well.
/// Used to indicate that names should revert to being looked up in the
/// defining scope.
void Sema::ActOnCXXExitDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");
  if (SS.isInvalid())
    return;
  assert(!SS.isInvalid() && computeDeclContext(SS, true) &&
         "exiting declarator scope we never really entered");
  ExitDeclaratorContext(S);
}
