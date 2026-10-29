#include "hikari/core/editor_syntax.h"

#include <gtest/gtest.h>

#include <vector>

using namespace hikari::core::syntax;

namespace {

using R = Role;
using Span = hikari::core::syntax::Run; // testing::Test has its own Run
using Runs = std::vector<Span>;
Runs runs(std::u16string_view text, TextState state = TextState::Dialogue)
{
    return colourRuns(text, state);
}

}

TEST(EditorSyntax, StateFollowsCommentAndEffect)
{
    EXPECT_EQ(textState(false, u"template line"), TextState::Dialogue);
    EXPECT_EQ(textState(true, u""), TextState::Comment);
    EXPECT_EQ(textState(true, u"template line"), TextState::Template);
    EXPECT_EQ(textState(true, u"templatefoo"), TextState::Template);
    EXPECT_EQ(textState(true, u"code once"), TextState::Code);
    EXPECT_EQ(textState(true, u"Template"), TextState::Comment);
    EXPECT_EQ(textState(true, u" code"), TextState::Comment);
    EXPECT_TRUE(spellChecked(TextState::Dialogue));
    EXPECT_FALSE(spellChecked(TextState::Comment));
    EXPECT_FALSE(spellChecked(TextState::Template));
    EXPECT_FALSE(spellChecked(TextState::Code));
}

TEST(EditorSyntax, OverrideTagsTakeBracesOperatorsNamesAndValues)
{
    EXPECT_TRUE(runs(u"").empty());
    EXPECT_EQ(runs(u"Hello"), (Runs{{0, 5, R::Text}}));
    EXPECT_EQ(runs(u"{\\fs20}Hello"), (Runs{{0, 1, R::CurlyBraces},
                                                         {1, 2, R::Operators},
                                                         {2, 4, R::Names},
                                                         {4, 6, R::Values},
                                                         {6, 7, R::CurlyBraces},
                                                         {7, 12, R::Text}}));
    EXPECT_EQ(runs(u"{\\pos(10,20)}"), (Runs{{0, 1, R::CurlyBraces},
                                                          {1, 2, R::Operators},
                                                          {2, 5, R::Names},
                                                          {5, 6, R::Operators},
                                                          {6, 8, R::Values},
                                                          {8, 9, R::Operators},
                                                          {9, 11, R::Values},
                                                          {11, 12, R::Operators},
                                                          {12, 13, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"{\\fnArial}"), (Runs{{0, 1, R::CurlyBraces},
                                                       {1, 2, R::Operators},
                                                       {2, 4, R::Names},
                                                       {4, 9, R::Values},
                                                       {9, 10, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"{\\1c&HFF&}"), (Runs{{0, 1, R::CurlyBraces},
                                                       {1, 2, R::Operators},
                                                       {2, 4, R::Names},
                                                       {4, 9, R::Values},
                                                       {9, 10, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"{\\b}x"), (Runs{{0, 1, R::CurlyBraces},
                                                  {1, 2, R::Operators},
                                                  {2, 3, R::Names},
                                                  {3, 4, R::CurlyBraces},
                                                  {4, 5, R::Text}}));
}

TEST(EditorSyntax, BreaksAndDrawingsTakeTheirColour)
{
    EXPECT_EQ(runs(u"a\\Nb"), (Runs{{0, 1, R::Text}, {1, 3, R::SplitAndDrawings}, {3, 4, R::Text}}));
    EXPECT_EQ(runs(u"a\\hb"), (Runs{{0, 1, R::Text}, {1, 3, R::SplitAndDrawings}, {3, 4, R::Text}}));
    EXPECT_EQ(runs(u"a\\xb"), (Runs{{0, 4, R::Text}}));
    EXPECT_EQ(runs(u"{\\p1}m 0 0{\\p0}x"), (Runs{{0, 1, R::CurlyBraces},
                                                               {1, 2, R::Operators},
                                                               {2, 3, R::Names},
                                                               {3, 4, R::Values},
                                                               {4, 5, R::CurlyBraces},
                                                               {5, 10, R::SplitAndDrawings},
                                                               {10, 11, R::CurlyBraces},
                                                               {11, 12, R::Operators},
                                                               {12, 13, R::Names},
                                                               {13, 14, R::Values},
                                                               {14, 15, R::CurlyBraces},
                                                               {15, 16, R::Text}}));
    EXPECT_EQ(runs(u"{\\p1}m 0 0").back(), (Span{5, 10, R::SplitAndDrawings}));
    EXPECT_EQ(runs(u"a\tb"), (Runs{{0, 1, R::Text}, {2, 3, R::Text}}));
}

TEST(EditorSyntax, CommentsAreColouredAsDialogue)
{
    EXPECT_EQ(runs(u"{\\fs20}if x", TextState::Comment), runs(u"{\\fs20}if x"));
    EXPECT_EQ(runs(u"!a.b!", TextState::Comment), (Runs{{0, 5, R::Text}}));
}

TEST(EditorSyntax, TemplateLinesColourTheirCode)
{
    EXPECT_EQ(runs(u"!line.start_time!", TextState::Template), (Runs{{0, 1, R::TemplateCodeMarks},
                                                                                {1, 5, R::TemplateVariables},
                                                                                {5, 6, R::Operators},
                                                                                {6, 16, R::TemplateVariables},
                                                                                {16, 17, R::TemplateCodeMarks}}));
    EXPECT_EQ(runs(u"ab cd+1.5 x", TextState::Template), (Runs{{0, 2, R::Text},
                                                                          {3, 5, R::TemplateVariables},
                                                                          {5, 6, R::Operators},
                                                                          {6, 9, R::Text},
                                                                          {10, 11, R::Text}}));
    EXPECT_EQ(runs(u"!x+1.5!", TextState::Template), (Runs{{0, 1, R::TemplateCodeMarks},
                                                                      {1, 2, R::TemplateVariables},
                                                                      {2, 3, R::Operators},
                                                                      {3, 6, R::Values},
                                                                      {6, 7, R::TemplateCodeMarks}}));
    EXPECT_EQ(runs(u"!if f(2) then!", TextState::Template), (Runs{{0, 1, R::TemplateCodeMarks},
                                                                             {1, 3, R::TemplateKeywords},
                                                                             {4, 5, R::TemplateFunctions},
                                                                             {5, 6, R::Operators},
                                                                             {6, 7, R::Values},
                                                                             {7, 8, R::Operators},
                                                                             {9, 13, R::TemplateKeywords},
                                                                             {13, 14, R::TemplateCodeMarks}}));
}

TEST(EditorSyntax, CodeLinesAreCodeThroughout)
{
    EXPECT_EQ(runs(u"if x then \"a b\" end", TextState::Code), (Runs{{0, 2, R::TemplateKeywords},
                                                                                {3, 4, R::TemplateVariables},
                                                                                {5, 9, R::TemplateKeywords},
                                                                                {10, 15, R::TemplateStrings},
                                                                                {16, 19, R::TemplateKeywords}}));
    EXPECT_EQ(runs(u"a!b", TextState::Code), (Runs{{0, 1, R::TemplateVariables},
                                                              {1, 2, R::TemplateCodeMarks},
                                                              {2, 3, R::TemplateVariables}}));
    EXPECT_EQ(runs(u"x=\"ab", TextState::Code), (Runs{{0, 1, R::TemplateVariables},
                                                                 {1, 2, R::Operators},
                                                                 {2, 5, R::TemplateStrings}}));
    EXPECT_EQ(runs(u"{\\b1}", TextState::Code), (Runs{{0, 1, R::CurlyBraces},
                                                                 {1, 2, R::Operators},
                                                                 {2, 3, R::Names},
                                                                 {3, 4, R::Values},
                                                                 {4, 5, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"ab{\\b1}", TextState::Code), (Runs{{0, 2, R::TemplateVariables},
                                                                   {2, 3, R::CurlyBraces},
                                                                   {3, 4, R::Operators},
                                                                   {4, 5, R::Names},
                                                                   {5, 6, R::Values},
                                                                   {6, 7, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"{x}", TextState::Code), (Runs{{0, 1, R::CurlyBraces},
                                                              {1, 2, R::TemplateVariables},
                                                              {2, 3, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"{a\\b1}", TextState::Code), (Runs{{0, 1, R::CurlyBraces},
                                                                  {1, 2, R::TemplateVariables},
                                                                  {2, 3, R::Operators},
                                                                  {3, 4, R::Names},
                                                                  {4, 5, R::Values},
                                                                  {5, 6, R::CurlyBraces}}));
    EXPECT_EQ(runs(u"a\\Nb", TextState::Code), (Runs{{0, 1, R::TemplateVariables},
                                                                 {1, 3, R::SplitAndDrawings},
                                                                 {3, 4, R::TemplateVariables}}));
}
